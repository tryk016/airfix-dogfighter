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
    src/airfix/assets/LegacyFormats.cpp
)
add_library(airfix::assets ALIAS airfix_assets)

target_include_directories(airfix_assets
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_compile_features(airfix_assets PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(airfix_assets PRIVATE /W4 /permissive-)
else()
    target_compile_options(airfix_assets PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(airfix_afpack
    src/airfix/package/AfPack.cpp
    src/airfix/package/AfPackWriter.cpp
)
add_library(airfix::afpack ALIAS airfix_afpack)

target_include_directories(airfix_afpack
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_compile_features(airfix_afpack PUBLIC cxx_std_20)
target_link_libraries(airfix_afpack PUBLIC airfix::crypto)

if(MSVC)
    target_compile_options(airfix_afpack PRIVATE /W4 /permissive-)
else()
    target_compile_options(airfix_afpack PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(AIRFIX_BUILD_TOOLS)
    add_executable(udsp-list tools/udsp/main.cpp)
    target_link_libraries(udsp-list PRIVATE airfix::udsp airfix::assets)

    add_executable(afpack-create tools/afpack/main.cpp)
    target_link_libraries(afpack-create PRIVATE airfix::afpack airfix::udsp)

    add_executable(gti-preview tools/gti-preview/main.cpp)
    target_link_libraries(gti-preview PRIVATE airfix::assets airfix::udsp)
endif()

if(AIRFIX_BUILD_IOS_APP)
    add_subdirectory(apps/airfix-ios)
endif()

if(AIRFIX_BUILD_TESTS)
    enable_testing()
    add_executable(airfix_udsp_tests tests/UdspArchiveTests.cpp)
    target_link_libraries(airfix_udsp_tests PRIVATE airfix::udsp)
    add_test(NAME airfix_udsp_tests COMMAND airfix_udsp_tests)

    add_executable(airfix_afpack_tests tests/AfPackTests.cpp)
    target_link_libraries(airfix_afpack_tests PRIVATE airfix::afpack)
    add_test(NAME airfix_afpack_tests COMMAND airfix_afpack_tests)

    add_executable(airfix_crypto_tests tests/Sha256Tests.cpp)
    target_link_libraries(airfix_crypto_tests PRIVATE airfix::crypto)
    add_test(NAME airfix_crypto_tests COMMAND airfix_crypto_tests)

    add_executable(airfix_asset_tests tests/LegacyFormatsTests.cpp)
    target_link_libraries(airfix_asset_tests PRIVATE airfix::assets)
    add_test(NAME airfix_asset_tests COMMAND airfix_asset_tests)

    add_executable(airfix_afchunk_tests tests/AfChunkContainerTests.cpp)
    target_link_libraries(airfix_afchunk_tests PRIVATE airfix::assets)
    add_test(NAME airfix_afchunk_tests COMMAND airfix_afchunk_tests)

    add_executable(airfix_runtime_tests tests/AppSessionTests.cpp)
    target_link_libraries(airfix_runtime_tests PRIVATE airfix::runtime)
    add_test(NAME airfix_runtime_tests COMMAND airfix_runtime_tests)
endif()
