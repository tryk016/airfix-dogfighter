cmake_minimum_required(VERSION 3.25)

project(AirfixDogfighterReconstruction VERSION 0.1.0 LANGUAGES CXX)

option(AIRFIX_BUILD_TESTS "Build reconstruction tests" ON)

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

add_executable(udsp-list tools/udsp/main.cpp)
target_link_libraries(udsp-list PRIVATE airfix::udsp)

if(AIRFIX_BUILD_TESTS)
    enable_testing()
    add_executable(airfix_udsp_tests tests/UdspArchiveTests.cpp)
    target_link_libraries(airfix_udsp_tests PRIVATE airfix::udsp)
    add_test(NAME airfix_udsp_tests COMMAND airfix_udsp_tests)
endif()
