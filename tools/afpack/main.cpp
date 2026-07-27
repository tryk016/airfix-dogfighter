#include "airfix/archive/UdspArchive.hpp"
#include "airfix/package/AfPack.hpp"
#include "airfix/package/AfPackWriter.hpp"

#include <exception>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct Language final {
    std::string fileStem;
    std::string locale;
};

[[nodiscard]] Language parseLanguage(const std::string_view value) {
    static const std::map<std::string_view, Language> languages{
        {"Dansk", {"Dansk", "da"}},
        {"English", {"English", "en"}},
        {"Norsk", {"Norsk", "no"}},
        {"Svenska", {"Svenska", "sv"}},
    };
    const auto found = languages.find(value);
    if (found == languages.end()) {
        throw std::runtime_error(
            "unsupported language; use Dansk, English, Norsk, or Svenska");
    }
    return found->second;
}

struct Arguments final {
    std::filesystem::path sourceRoot;
    std::filesystem::path outputPath;
    Language language;
    std::string commit{"unknown"};
    bool hasLanguage{};
};

[[nodiscard]] Arguments parseArguments(const int argc, const char* const* argv) {
    Arguments arguments;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option = argv[index];
        if (option == "--help") {
            std::cout
                << "usage: afpack-create --source <installation> --language "
                   "<English|Dansk|Norsk|Svenska> --output <file.afpack> "
                   "[--commit <git-id>]\n";
            std::exit(0);
        }
        if (index + 1 >= argc) {
            throw std::runtime_error("missing value for option: " + std::string(option));
        }
        const std::string value = argv[++index];
        if (option == "--source") {
            arguments.sourceRoot = value;
        }
        else if (option == "--output") {
            arguments.outputPath = value;
        }
        else if (option == "--language") {
            arguments.language = parseLanguage(value);
            arguments.hasLanguage = true;
        }
        else if (option == "--commit") {
            arguments.commit = value;
        }
        else {
            throw std::runtime_error("unknown option: " + std::string(option));
        }
    }
    if (arguments.sourceRoot.empty() || arguments.outputPath.empty() ||
        !arguments.hasLanguage) {
        throw std::runtime_error(
            "--source, --language, and --output are required; use --help");
    }
    return arguments;
}

} // namespace

int main(const int argc, const char* const* argv) {
    try {
        const auto arguments = parseArguments(argc, argv);
        if (!std::filesystem::is_directory(arguments.sourceRoot)) {
            throw std::runtime_error("source installation is not a directory: " +
                arguments.sourceRoot.string());
        }

        const auto resource = arguments.sourceRoot / "Resource.up";
        const auto localization = arguments.sourceRoot /
            (arguments.language.fileStem + ".up");

        // Rejecting executable/plugin inputs is structural: this initial tool
        // constructs its complete allowlist itself and accepts no arbitrary
        // input file arguments.
        const auto resourceArchive = airfix::udsp::Archive::open(resource);
        const auto localizationArchive = airfix::udsp::Archive::open(localization);

        const airfix::afpack::WriteRequest request{
            .outputPath = arguments.outputPath,
            .manifest = {
                .sourceVersion = "1.01",
                .converterVersion = "0.1.0",
                .converterCommit = arguments.commit,
                .locale = arguments.language.locale,
            },
            .entries = {
                {
                    .logicalPath = "localization/" + arguments.language.fileStem + ".up",
                    .kind = airfix::afpack::EntryKind::localization,
                    .sourcePath = localization,
                },
                {
                    .logicalPath = "source/Resource.up",
                    .kind = airfix::afpack::EntryKind::sourceArchive,
                    .sourcePath = resource,
                },
            },
        };
        const auto result = airfix::afpack::writePack(request);

        const auto pack = airfix::afpack::Pack::open(arguments.outputPath);
        pack.verifyPayloads(arguments.outputPath);
        for (const auto& entry : pack.entries()) {
            if (entry.kind == airfix::afpack::EntryKind::sourceArchive ||
                entry.kind == airfix::afpack::EntryKind::localization) {
                (void)airfix::udsp::Archive::openRegion(
                    arguments.outputPath, entry.dataOffset, entry.storedSize);
            }
        }

        std::cout << "created=" << arguments.outputPath.string()
                  << " bytes=" << result.archiveSize
                  << " entries=" << result.entries.size()
                  << " resourceFiles=" << resourceArchive.files().size()
                  << " localizationFiles=" << localizationArchive.files().size()
                  << " locale=" << arguments.language.locale << '\n';
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "afpack-create: " << error.what() << '\n';
        return 1;
    }
}
