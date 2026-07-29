include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "Do not build a shared SDL library" FORCE)
set(SDL_STATIC ON CACHE BOOL "Build the static SDL library" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "Do not build the SDL test library" FORCE)
set(SDL_TESTS OFF CACHE BOOL "Do not build SDL tests" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "Do not build SDL examples" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "Do not install fetched SDL" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "Windows audio is a separate native adapter" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "The Windows product does not use SDL cameras" FORCE)
set(SDL_DIALOG OFF CACHE BOOL "The Windows product does not use SDL dialogs" FORCE)
set(SDL_GPU OFF CACHE BOOL "Rendering is owned by native D3D11" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "Haptics are outside the first shell" FORCE)
set(SDL_POWER OFF CACHE BOOL "Power integration is outside the first shell" FORCE)
set(SDL_RENDER OFF CACHE BOOL "Rendering is owned by native D3D11" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "Sensors are outside the Windows product" FORCE)
set(SDL_TRAY OFF CACHE BOOL "The Windows product does not use a tray icon" FORCE)

FetchContent_Declare(SDL3
    URL
        "https://github.com/libsdl-org/SDL/releases/download/release-3.4.12/SDL3-3.4.12.tar.gz"
    URL_HASH
        "SHA256=f07b958a9ac5020fb7a44cadb957f658b2149c3c8abb4f63145fac9303249db7"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(SDL3)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/AirfixSmoke.hlsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/AirfixSmoke.hlsl"
    AIRFIX_WINDOWS_SMOKE_HLSL)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/AirfixEmbeddedShader.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/AirfixEmbeddedShader.hpp"
    @ONLY
)

add_executable(airfix_windows WIN32
    AirfixD3D11Renderer.cpp
    AirfixD3D11Renderer.hpp
    AirfixSmoke.hlsl
    AirfixWindowsApp.cpp
)
set_source_files_properties(AirfixSmoke.hlsl PROPERTIES HEADER_FILE_ONLY TRUE)

target_include_directories(airfix_windows PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_compile_features(airfix_windows PRIVATE cxx_std_20)
target_compile_definitions(airfix_windows PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
if(MSVC)
    target_compile_options(airfix_windows PRIVATE /W4 /permissive-)
else()
    target_compile_options(airfix_windows PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()
target_link_libraries(airfix_windows PRIVATE
    airfix::render
    airfix::runtime
    SDL3::SDL3-static
    d3d11
    d3dcompiler
    dxgi
)

set_target_properties(airfix_windows PROPERTIES
    OUTPUT_NAME "AirfixDogfighter"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:airfix_windows>"
)

add_custom_command(TARGET airfix_windows POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "$<TARGET_FILE_DIR:airfix_windows>/third-party-licenses"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${sdl3_SOURCE_DIR}/LICENSE.txt"
        "$<TARGET_FILE_DIR:airfix_windows>/third-party-licenses/SDL3.txt"
    COMMENT "Copying the SDL3 zlib license"
    VERBATIM
)

if(AIRFIX_BUILD_TESTS)
    add_test(NAME airfix_windows_gpu_smoke_tests
        COMMAND airfix_windows --smoke-test)
    set_tests_properties(airfix_windows_gpu_smoke_tests PROPERTIES
        TIMEOUT 30
    )
endif()
