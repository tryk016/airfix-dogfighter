cmake_minimum_required(VERSION 3.25)

project(AirfixDogfighterReconstruction VERSION 0.1.0 LANGUAGES CXX)

option(AIRFIX_BUILD_TESTS "Build reconstruction tests" ON)
option(AIRFIX_BUILD_TOOLS "Build desktop reconstruction tools" ON)
option(AIRFIX_BUILD_IOS_APP "Build the data-less iOS application shell" OFF)
set(AIRFIX_IOS_DEPLOYMENT_TARGET "16.4" CACHE STRING
    "Minimum iOS version supported by the application")

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    enable_language(OBJCXX)
endif()

if(AIRFIX_BUILD_IOS_APP AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(FATAL_ERROR "AIRFIX_BUILD_IOS_APP requires an iOS CMake toolchain")
endif()

add_library(airfix_crypto
    src/airfix/crypto/Sha256.cpp
)
add_library(airfix::crypto ALIAS airfix_crypto)

target_include_directories(airfix_crypto
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_compile_features(airfix_crypto PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(airfix_crypto PRIVATE /W4 /permissive-)
else()
    target_compile_options(airfix_crypto PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(airfix_runtime
    src/airfix/runtime/AppSession.cpp
)
add_library(airfix::runtime ALIAS airfix_runtime)

target_include_directories(airfix_runtime
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_compile_features(airfix_runtime PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(airfix_runtime PRIVATE /W4 /permissive-)
else()
    target_compile_options(airfix_runtime PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(airfix_input
    src/airfix/input/InputRouter.cpp
)
add_library(airfix::input ALIAS airfix_input)

target_include_directories(airfix_input
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_compile_features(airfix_input PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(airfix_input PRIVATE /W4 /permissive-)
else()
    target_compile_options(airfix_input PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(airfix_udsp
    src/airfix/archive/UdspArchive.cpp
)
add_library(airfix::udsp ALIAS airfix_udsp)

target_include_directories(airfix_udsp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_compile_features(airfix_udsp PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(airfix_udsp PRIVATE /W4 /permissive-)
else()
    target_compile_options(airfix_udsp PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(airfix_assets
    src/airfix/assets/AfChunkContainer.cpp
    src/airfix/assets/AssetResolver.cpp
    src/airfix/assets/CcfBlueprintGraph.cpp
    src/airfix/assets/LegacyFormats.cpp
    src/airfix/assets/MissionEntryResolver.cpp
)
add_library(airfix::assets ALIAS airfix_assets)

target_include_directories(airfix_assets
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_compile_features(airfix_assets PUBLIC cxx_std_20)
target_link_libraries(airfix_assets PUBLIC airfix::udsp)

if(MSVC)
    target_compile_options(airfix_assets PRIVATE /W4 /permissive-)
else()
    target_compile_options(airfix_assets PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(airfix_render
    src/airfix/render/DiagnosticRasterizer.cpp
    src/airfix/render/DrawMesh.cpp
    src/airfix/render/LegacyGeometry.cpp
)
add_library(airfix::render ALIAS airfix_render)

target_include_directories(airfix_render
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_compile_features(airfix_render PUBLIC cxx_std_20)
target_link_libraries(airfix_render PUBLIC airfix::assets)

if(MSVC)
    target_compile_options(airfix_render PRIVATE /W4 /permissive-)
else()
    target_compile_options(airfix_render PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(airfix_io)
add_library(airfix::io ALIAS airfix_io)
if(WIN32)
    target_sources(airfix_io PRIVATE src/airfix/io/DurableFileWin32.cpp)
else()
    target_sources(airfix_io PRIVATE src/airfix/io/DurableFilePosix.cpp)
endif()
target_include_directories(airfix_io
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_compile_features(airfix_io PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(airfix_io PRIVATE /W4 /permissive-)
else()
    target_compile_options(airfix_io PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(airfix_afpack
    src/airfix/package/AfPack.cpp
    src/airfix/package/AfPackActiveRecord.cpp
    src/airfix/package/AfPackInstaller.cpp
    src/airfix/package/AfPackManifest.cpp
    src/airfix/package/AfPackRecovery.cpp
    src/airfix/package/AfPackValidation.cpp
    src/airfix/package/AfPackWriter.cpp
)
add_library(airfix::afpack ALIAS airfix_afpack)

target_include_directories(airfix_afpack
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_compile_features(airfix_afpack PUBLIC cxx_std_20)
target_link_libraries(airfix_afpack PUBLIC airfix::crypto airfix::io airfix::udsp)

if(MSVC)
    target_compile_options(airfix_afpack PRIVATE /W4 /permissive-)
else()
    target_compile_options(airfix_afpack PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(AIRFIX_BUILD_TOOLS)
    add_executable(udsp-list tools/udsp/main.cpp)
    target_link_libraries(udsp-list PRIVATE airfix::udsp airfix::assets airfix::render)

    add_executable(afpack-create tools/afpack/main.cpp)
    target_link_libraries(afpack-create PRIVATE airfix::afpack airfix::udsp)

    add_executable(gti-preview tools/gti-preview/main.cpp)
    target_link_libraries(gti-preview PRIVATE airfix::assets airfix::udsp)

    add_executable(model-preview tools/model-preview/main.cpp)
    target_link_libraries(model-preview
        PRIVATE airfix::udsp airfix::assets airfix::render)
endif()

if(AIRFIX_BUILD_IOS_APP)
    add_subdirectory(apps/airfix-ios)
endif()

if(AIRFIX_BUILD_TESTS)
    enable_testing()
    add_executable(airfix_udsp_tests tests/UdspArchiveTests.cpp)
    target_link_libraries(airfix_udsp_tests PRIVATE airfix::udsp)
    add_test(NAME airfix_udsp_tests COMMAND airfix_udsp_tests)

    add_executable(airfix_udsp_limits_tests tests/UdspLimitsTests.cpp)
    target_link_libraries(airfix_udsp_limits_tests PRIVATE airfix::udsp)
    add_test(NAME airfix_udsp_limits_tests COMMAND airfix_udsp_limits_tests)

    add_executable(airfix_afpack_tests tests/AfPackTests.cpp)
    target_link_libraries(airfix_afpack_tests PRIVATE airfix::afpack)
    add_test(NAME airfix_afpack_tests COMMAND airfix_afpack_tests)

    add_executable(airfix_afpack_limits_tests tests/AfPackLimitsTests.cpp)
    target_link_libraries(airfix_afpack_limits_tests PRIVATE airfix::afpack)
    add_test(NAME airfix_afpack_limits_tests COMMAND airfix_afpack_limits_tests)

    add_executable(airfix_afpack_manifest_tests tests/AfPackManifestTests.cpp)
    target_link_libraries(airfix_afpack_manifest_tests PRIVATE airfix::afpack)
    add_test(NAME airfix_afpack_manifest_tests COMMAND airfix_afpack_manifest_tests)

    add_executable(airfix_afpack_validation_tests tests/AfPackValidationTests.cpp)
    target_link_libraries(airfix_afpack_validation_tests PRIVATE airfix::afpack)
    add_test(NAME airfix_afpack_validation_tests COMMAND airfix_afpack_validation_tests)

    add_executable(airfix_afpack_active_record_tests tests/AfPackActiveRecordTests.cpp)
    target_link_libraries(airfix_afpack_active_record_tests PRIVATE airfix::afpack)
    add_test(NAME airfix_afpack_active_record_tests COMMAND airfix_afpack_active_record_tests)

    add_executable(airfix_afpack_installer_tests tests/AfPackInstallerTests.cpp)
    target_link_libraries(airfix_afpack_installer_tests PRIVATE airfix::afpack)
    add_test(NAME airfix_afpack_installer_tests COMMAND airfix_afpack_installer_tests)

    add_executable(airfix_afpack_recovery_tests tests/AfPackRecoveryTests.cpp)
    target_link_libraries(airfix_afpack_recovery_tests PRIVATE airfix::afpack)
    add_test(NAME airfix_afpack_recovery_tests COMMAND airfix_afpack_recovery_tests)

    add_executable(airfix_crypto_tests tests/Sha256Tests.cpp)
    target_link_libraries(airfix_crypto_tests PRIVATE airfix::crypto)
    add_test(NAME airfix_crypto_tests COMMAND airfix_crypto_tests)

    add_executable(airfix_durable_file_tests tests/DurableFileTests.cpp)
    target_link_libraries(airfix_durable_file_tests PRIVATE airfix::io)
    add_test(NAME airfix_durable_file_tests COMMAND airfix_durable_file_tests)

    add_executable(airfix_asset_tests tests/LegacyFormatsTests.cpp)
    target_link_libraries(airfix_asset_tests PRIVATE airfix::assets)
    add_test(NAME airfix_asset_tests COMMAND airfix_asset_tests)

    add_executable(airfix_afchunk_tests tests/AfChunkContainerTests.cpp)
    target_link_libraries(airfix_afchunk_tests PRIVATE airfix::assets)
    add_test(NAME airfix_afchunk_tests COMMAND airfix_afchunk_tests)

    add_executable(airfix_asset_resolver_tests tests/AssetResolverTests.cpp)
    target_link_libraries(airfix_asset_resolver_tests PRIVATE airfix::assets)
    add_test(NAME airfix_asset_resolver_tests COMMAND airfix_asset_resolver_tests)

    add_executable(airfix_mission_entry_resolver_tests
        tests/MissionEntryResolverTests.cpp)
    target_link_libraries(airfix_mission_entry_resolver_tests PRIVATE airfix::assets)
    add_test(NAME airfix_mission_entry_resolver_tests
        COMMAND airfix_mission_entry_resolver_tests)

    add_executable(airfix_blueprint_graph_tests tests/CcfBlueprintGraphTests.cpp)
    target_link_libraries(airfix_blueprint_graph_tests PRIVATE airfix::assets)
    add_test(NAME airfix_blueprint_graph_tests COMMAND airfix_blueprint_graph_tests)

    add_executable(airfix_runtime_tests tests/AppSessionTests.cpp)
    target_link_libraries(airfix_runtime_tests PRIVATE airfix::runtime)
    add_test(NAME airfix_runtime_tests COMMAND airfix_runtime_tests)

    add_executable(airfix_input_tests tests/InputRouterTests.cpp)
    target_link_libraries(airfix_input_tests PRIVATE airfix::input)
    add_test(NAME airfix_input_tests COMMAND airfix_input_tests)

    add_executable(airfix_render_tests tests/LegacyGeometryTests.cpp)
    target_link_libraries(airfix_render_tests PRIVATE airfix::render)
    add_test(NAME airfix_render_tests COMMAND airfix_render_tests)

    add_executable(airfix_draw_mesh_tests tests/DrawMeshTests.cpp)
    target_link_libraries(airfix_draw_mesh_tests PRIVATE airfix::render)
    add_test(NAME airfix_draw_mesh_tests COMMAND airfix_draw_mesh_tests)

    add_executable(airfix_diagnostic_rasterizer_tests
        tests/DiagnosticRasterizerTests.cpp)
    target_link_libraries(airfix_diagnostic_rasterizer_tests PRIVATE airfix::render)
    add_test(NAME airfix_diagnostic_rasterizer_tests
        COMMAND airfix_diagnostic_rasterizer_tests)
endif()
