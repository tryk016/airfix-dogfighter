#include "airfix/io/DurableFile.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] std::span<const std::uint8_t> bytes(const std::string_view value) {
    return {
        reinterpret_cast<const std::uint8_t*>(value.data()),
        value.size(),
    };
}

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot read test file: " + path.string());
    }
    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>(),
    };
}

void requireError(
    const std::function<void()>& action,
    const airfix::io::DurableFileErrorKind kind) {
    try {
        action();
    }
    catch (const airfix::io::DurableFileError& error) {
        require(error.kind() == kind, "durable file error kind mismatch");
        return;
    }
    throw std::runtime_error("expected DurableFileError");
}

class TempDirectory final {
public:
    TempDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned int attempt = 0U; attempt < 100U; ++attempt) {
            path_ = std::filesystem::temp_directory_path() /
                ("airfix-durable-file-" + std::to_string(stamp) + "-" +
                    std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) {
                return;
            }
            if (error && error != std::errc::file_exists) {
                throw std::runtime_error(
                    "cannot create durable-file test directory: " + error.message());
            }
        }
        throw std::runtime_error("cannot allocate unique durable-file test directory");
    }

    ~TempDirectory() {
        static constexpr std::string_view knownFiles[] {
            "exclusive.bin",
            "prepared-existing.bin",
            "existing.bin",
            "prepared-publish.bin",
            "published.bin",
            "prepared-replace.bin",
            "replace-target.bin",
            "same-prepared.bin",
            "same-target.bin",
            "alias.bin",
            "race-publish-prepared.bin",
            "race-publish-substitute.bin",
            "race-publish-parked.bin",
            "race-publish-final.bin",
            "race-replace-prepared.bin",
            "race-replace-substitute.bin",
            "race-replace-parked.bin",
            "race-replace-target.bin",
            "sync.bin",
        };
        for (const auto name : knownFiles) {
            std::error_code error;
            std::filesystem::remove(path_ / name, error);
        }
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    [[nodiscard]] std::filesystem::path file(const std::string_view name) const {
        return path_ / name;
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

class CurrentPathGuard final {
public:
    explicit CurrentPathGuard(const std::filesystem::path& path)
        : original_(std::filesystem::current_path()) {
        std::filesystem::current_path(path);
    }

    ~CurrentPathGuard() {
        std::error_code error;
        std::filesystem::current_path(original_, error);
    }

private:
    std::filesystem::path original_;
};

struct SwapPreparedContext {
    std::filesystem::path prepared;
    std::filesystem::path substitute;
    std::filesystem::path parked;
};

void swapPrepared(void* const opaqueContext) {
    auto& context = *static_cast<SwapPreparedContext*>(opaqueContext);
    std::error_code error;
    std::filesystem::rename(context.prepared, context.parked, error);
    if (error) {
        throw std::runtime_error("cannot park verified test file: " + error.message());
    }
    std::filesystem::rename(context.substitute, context.prepared, error);
    if (!error) {
        return;
    }
    std::error_code rollbackError;
    std::filesystem::rename(context.parked, context.prepared, rollbackError);
    throw std::runtime_error("cannot substitute prepared test file: " + error.message());
}

void testExclusiveWrite(TempDirectory& temporary) {
    const auto path = temporary.file("exclusive.bin");
    airfix::io::writeFileExclusiveDurable(
        path, bytes(std::string_view("exact\0bytes", 11U)));
    require(readFile(path) == std::string("exact\0bytes", 11U),
        "exclusive durable write changed content");

    requireError(
        [&] { airfix::io::writeFileExclusiveDurable(path, bytes("replacement")); },
        airfix::io::DurableFileErrorKind::alreadyExists);
    require(readFile(path) == std::string("exact\0bytes", 11U),
        "exclusive durable write replaced existing content");
}

void testNoReplace(TempDirectory& temporary) {
    const auto preparedExisting = temporary.file("prepared-existing.bin");
    const auto existing = temporary.file("existing.bin");
    airfix::io::writeFileExclusiveDurable(preparedExisting, bytes("prepared"));
    airfix::io::writeFileExclusiveDurable(existing, bytes("existing"));
    requireError(
        [&] { airfix::io::renameFileNoReplaceDurable(preparedExisting, existing); },
        airfix::io::DurableFileErrorKind::alreadyExists);
    require(readFile(preparedExisting) == "prepared",
        "failed no-replace removed its source");
    require(readFile(existing) == "existing",
        "failed no-replace changed its destination");

    const auto preparedPublish = temporary.file("prepared-publish.bin");
    const auto published = temporary.file("published.bin");
    airfix::io::writeFileExclusiveDurable(preparedPublish, bytes("published-content"));
    airfix::io::renameFileNoReplaceDurable(preparedPublish, published);
    require(!std::filesystem::exists(preparedPublish),
        "successful no-replace left its source name");
    require(readFile(published) == "published-content",
        "successful no-replace changed content");
}

void testReplace(TempDirectory& temporary) {
    const auto prepared = temporary.file("prepared-replace.bin");
    const auto target = temporary.file("replace-target.bin");
    airfix::io::writeFileExclusiveDurable(prepared, bytes("new-content"));
    airfix::io::writeFileExclusiveDurable(target, bytes("old-content"));
    airfix::io::replaceFileDurable(prepared, target);
    require(!std::filesystem::exists(prepared),
        "successful durable replacement left prepared path");
    require(readFile(target) == "new-content",
        "durable replacement retained old content");

    const auto samePrepared = temporary.file("same-prepared.bin");
    const auto sameTarget = temporary.file("same-target.bin");
    airfix::io::writeFileExclusiveDurable(samePrepared, bytes("same-inode"));
    std::error_code linkError;
    std::filesystem::create_hard_link(samePrepared, sameTarget, linkError);
    if (linkError) {
        throw std::runtime_error("cannot create hard-link test fixture: " +
            linkError.message());
    }
    requireError(
        [&] { airfix::io::replaceFileDurable(samePrepared, sameTarget); },
        airfix::io::DurableFileErrorKind::invalidArgument);
    require(std::filesystem::exists(samePrepared) && std::filesystem::exists(sameTarget),
        "same-file replacement changed either directory entry");

    const auto alias = temporary.file("alias.bin");
    airfix::io::writeFileExclusiveDurable(alias, bytes("alias"));
    {
        CurrentPathGuard currentPath(temporary.path());
        requireError(
            [&] {
                airfix::io::replaceFileDurable(
                    std::filesystem::path("alias.bin"), alias);
            },
            airfix::io::DurableFileErrorKind::invalidArgument);
    }
    require(readFile(alias) == "alias",
        "relative/absolute alias replacement changed its source");
}

void testPreparedSubstitution(TempDirectory& temporary) {
    SwapPreparedContext publish {
        .prepared = temporary.file("race-publish-prepared.bin"),
        .substitute = temporary.file("race-publish-substitute.bin"),
        .parked = temporary.file("race-publish-parked.bin"),
    };
    const auto finalPath = temporary.file("race-publish-final.bin");
    airfix::io::writeFileExclusiveDurable(publish.prepared, bytes("verified"));
    airfix::io::writeFileExclusiveDurable(publish.substitute, bytes("foreign"));
    requireError(
        [&] {
            airfix::io::testing::renameFileNoReplaceDurableWithHook(
                publish.prepared, finalPath, swapPrepared, &publish);
        },
        airfix::io::DurableFileErrorKind::ioFailure);
    require(!std::filesystem::exists(finalPath),
        "source substitution left a foreign published path");
    require(readFile(publish.prepared) == "foreign" &&
            readFile(publish.parked) == "verified",
        "source substitution test did not preserve both source identities");

    SwapPreparedContext replace {
        .prepared = temporary.file("race-replace-prepared.bin"),
        .substitute = temporary.file("race-replace-substitute.bin"),
        .parked = temporary.file("race-replace-parked.bin"),
    };
    const auto target = temporary.file("race-replace-target.bin");
    airfix::io::writeFileExclusiveDurable(replace.prepared, bytes("verified-new"));
    airfix::io::writeFileExclusiveDurable(replace.substitute, bytes("foreign-new"));
    airfix::io::writeFileExclusiveDurable(target, bytes("old-target"));
    requireError(
        [&] {
            airfix::io::testing::replaceFileDurableWithHook(
                replace.prepared, target, swapPrepared, &replace);
        },
        airfix::io::DurableFileErrorKind::ioFailure);
    require(readFile(target) == "old-target",
        "source substitution replaced target with a foreign file");
    require(readFile(replace.prepared) == "foreign-new" &&
            readFile(replace.parked) == "verified-new",
        "replacement substitution test lost a source identity");
}

void testSyncAndErrors(TempDirectory& temporary) {
    const auto regular = temporary.file("sync.bin");
    airfix::io::writeFileExclusiveDurable(regular, bytes("sync"));
    airfix::io::syncFile(regular);
    airfix::io::syncDirectory(temporary.path());

    requireError(
        [&] { airfix::io::syncFile(temporary.file("missing.bin")); },
        airfix::io::DurableFileErrorKind::notFound);
    requireError(
        [&] { airfix::io::syncDirectory(temporary.file("missing-directory")); },
        airfix::io::DurableFileErrorKind::notFound);
    requireError(
        [&] { airfix::io::syncFile(temporary.path()); },
        airfix::io::DurableFileErrorKind::wrongType);
    requireError(
        [&] { airfix::io::syncDirectory(regular); },
        airfix::io::DurableFileErrorKind::wrongType);
    requireError(
        [&] { airfix::io::writeFileExclusiveDurable({}, bytes("x")); },
        airfix::io::DurableFileErrorKind::invalidArgument);
    requireError(
        [&] { airfix::io::replaceFileDurable({}, regular); },
        airfix::io::DurableFileErrorKind::invalidArgument);
    requireError(
        [&] { airfix::io::renameFileNoReplaceDurable(regular, {}); },
        airfix::io::DurableFileErrorKind::invalidArgument);
    requireError(
        [&] { airfix::io::replaceFileDurable(regular, regular); },
        airfix::io::DurableFileErrorKind::invalidArgument);
    requireError(
        [&] {
            airfix::io::replaceFileDurable(
                temporary.file("missing-prepared.bin"), regular);
        },
        airfix::io::DurableFileErrorKind::notFound);
    requireError(
        [&] {
            airfix::io::renameFileNoReplaceDurable(
                temporary.file("missing-publish.bin"),
                temporary.file("missing-published.bin"));
        },
        airfix::io::DurableFileErrorKind::notFound);
    requireError(
        [&] { airfix::io::replaceFileDurable(regular, temporary.path()); },
        airfix::io::DurableFileErrorKind::wrongType);
}

} // namespace

int main() {
    try {
        TempDirectory temporary;
        testExclusiveWrite(temporary);
        testNoReplace(temporary);
        testReplace(temporary);
        testPreparedSubstitution(temporary);
        testSyncAndErrors(temporary);
        std::cout << "all durable-file tests passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "durable-file test failure: " << error.what() << '\n';
        return 1;
    }
}
