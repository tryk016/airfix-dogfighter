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

add_library(airfix_windows_input STATIC
    AirfixSdlInputAdapter.cpp
    AirfixSdlInputAdapter.hpp
)
target_include_directories(airfix_windows_input PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_features(airfix_windows_input PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(airfix_windows_input PRIVATE /W4 /permissive-)
else()
    target_compile_options(airfix_windows_input PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()
target_link_libraries(airfix_windows_input PUBLIC
    airfix::input
    SDL3::SDL3-static
)

add_library(airfix_windows_command_line STATIC
    AirfixWindowsCommandLine.cpp
    AirfixWindowsCommandLine.hpp
)
target_include_directories(airfix_windows_command_line PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_features(airfix_windows_command_line PUBLIC cxx_std_20)
target_link_libraries(airfix_windows_command_line PUBLIC
    airfix::render
)
if(MSVC)
    target_compile_options(airfix_windows_command_line PRIVATE
        /W4 /permissive-)
else()
    target_compile_options(airfix_windows_command_line PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

add_library(airfix_windows_settings_root STATIC
    AirfixWindowsSettingsRoot.cpp
    AirfixWindowsSettingsRoot.hpp
)
target_include_directories(airfix_windows_settings_root PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_features(airfix_windows_settings_root PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(airfix_windows_settings_root PRIVATE
        /W4 /permissive-)
else()
    target_compile_options(airfix_windows_settings_root PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()
target_link_libraries(airfix_windows_settings_root PRIVATE
    SDL3::SDL3-static
)

add_library(airfix_windows_content_import STATIC
    AirfixWindowsContentImport.cpp
    AirfixWindowsContentImport.hpp
)
target_include_directories(airfix_windows_content_import PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_features(airfix_windows_content_import PUBLIC cxx_std_20)
target_compile_definitions(airfix_windows_content_import PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
if(MSVC)
    target_compile_options(airfix_windows_content_import PRIVATE
        /W4 /permissive-)
else()
    target_compile_options(airfix_windows_content_import PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()
target_link_libraries(airfix_windows_content_import
    PUBLIC
        airfix::afpack
    PRIVATE
        ole32
)

add_library(airfix_windows_content_bootstrap STATIC
    AirfixWindowsContentBootstrap.cpp
    AirfixWindowsContentBootstrap.hpp
)
target_include_directories(airfix_windows_content_bootstrap PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_features(airfix_windows_content_bootstrap PUBLIC cxx_std_20)
target_compile_definitions(airfix_windows_content_bootstrap PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
if(MSVC)
    target_compile_options(airfix_windows_content_bootstrap PRIVATE
        /W4 /permissive-)
else()
    target_compile_options(airfix_windows_content_bootstrap PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()
target_link_libraries(airfix_windows_content_bootstrap
    PUBLIC
        airfix_windows_content_import
    PRIVATE
        ole32
        shell32
        user32
        uuid
)

add_library(airfix_windows_settings_panel STATIC
    AirfixWindowsRenderSettingsPanel.cpp
    AirfixWindowsRenderSettingsPanel.hpp
)
target_include_directories(airfix_windows_settings_panel PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_features(airfix_windows_settings_panel PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(airfix_windows_settings_panel PRIVATE
        /W4 /permissive-)
else()
    target_compile_options(airfix_windows_settings_panel PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()
target_link_libraries(airfix_windows_settings_panel PUBLIC
    airfix::input
    airfix::settings
)

add_library(airfix_windows_ui_semantics STATIC
    AirfixWindowsUiSemantics.cpp
    AirfixWindowsUiSemantics.hpp
)
target_include_directories(airfix_windows_ui_semantics PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_features(airfix_windows_ui_semantics PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(airfix_windows_ui_semantics PRIVATE
        /W4 /permissive-)
else()
    target_compile_options(airfix_windows_ui_semantics PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()
target_link_libraries(airfix_windows_ui_semantics PUBLIC
    airfix_windows_settings_panel
)

add_library(airfix_windows_ui_automation STATIC
    AirfixWindowsUiAutomation.cpp
    AirfixWindowsUiAutomation.hpp
)
target_include_directories(airfix_windows_ui_automation PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_features(airfix_windows_ui_automation PUBLIC cxx_std_20)
target_compile_definitions(airfix_windows_ui_automation PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
if(MSVC)
    target_compile_options(airfix_windows_ui_automation PRIVATE
        /W4 /permissive-)
else()
    target_compile_options(airfix_windows_ui_automation PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()
target_link_libraries(airfix_windows_ui_automation
    PUBLIC
        airfix_windows_ui_semantics
    PRIVATE
        comctl32
        ole32
        oleaut32
        uiautomationcore
        user32
)

add_library(airfix_windows_ui_rasterizer STATIC
    AirfixWindowsUiRasterizer.cpp
    AirfixWindowsUiRasterizer.hpp
)
target_include_directories(airfix_windows_ui_rasterizer PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_features(airfix_windows_ui_rasterizer PUBLIC cxx_std_20)
target_compile_definitions(airfix_windows_ui_rasterizer PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
if(MSVC)
    target_compile_options(airfix_windows_ui_rasterizer PRIVATE
        /W4 /permissive-)
else()
    target_compile_options(airfix_windows_ui_rasterizer PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()
target_link_libraries(airfix_windows_ui_rasterizer
    PUBLIC
        airfix_windows_ui_semantics
    PRIVATE
        d2d1
        dwrite
        ole32
        windowscodecs
)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/AirfixSmoke.hlsl")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/AirfixSmoke.hlsl"
    AIRFIX_WINDOWS_SMOKE_HLSL)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/AirfixEmbeddedShader.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/AirfixEmbeddedShader.hpp"
    @ONLY
)

add_library(airfix_windows_renderer STATIC
    AirfixD3D11Renderer.cpp
    AirfixD3D11Renderer.hpp
    AirfixSmoke.hlsl
)
set_source_files_properties(AirfixSmoke.hlsl PROPERTIES HEADER_FILE_ONLY TRUE)
target_include_directories(airfix_windows_renderer PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_include_directories(airfix_windows_renderer PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_compile_features(airfix_windows_renderer PUBLIC cxx_std_20)
target_compile_definitions(airfix_windows_renderer PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
if(MSVC)
    target_compile_options(airfix_windows_renderer PRIVATE
        /W4 /permissive-)
else()
    target_compile_options(airfix_windows_renderer PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()
target_link_libraries(airfix_windows_renderer
    PUBLIC
        airfix::content
        airfix::render
        airfix_windows_ui_rasterizer
    PRIVATE
        SDL3::SDL3-static
        d3d11
        d3dcompiler
        dxgi
)

add_library(airfix_windows_controller_profile_coordinator STATIC
    AirfixWindowsControllerProfileCoordinator.cpp
    AirfixWindowsControllerProfileCoordinator.hpp
)
target_include_directories(
    airfix_windows_controller_profile_coordinator PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_features(
    airfix_windows_controller_profile_coordinator PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(
        airfix_windows_controller_profile_coordinator PRIVATE
        /W4 /permissive-)
else()
    target_compile_options(
        airfix_windows_controller_profile_coordinator PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()
target_link_libraries(
    airfix_windows_controller_profile_coordinator PUBLIC
    airfix::settings
)

add_library(airfix_windows_render_settings_coordinator STATIC
    AirfixWindowsRenderSettingsCoordinator.cpp
    AirfixWindowsRenderSettingsCoordinator.hpp
)
target_include_directories(airfix_windows_render_settings_coordinator PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_features(airfix_windows_render_settings_coordinator
    PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(
        airfix_windows_render_settings_coordinator PRIVATE
        /W4 /permissive-)
else()
    target_compile_options(
        airfix_windows_render_settings_coordinator PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()
target_link_libraries(airfix_windows_render_settings_coordinator PUBLIC
    airfix_windows_renderer
    airfix::settings
)

add_executable(airfix_windows WIN32
    AirfixWindows.manifest
    AirfixWindowsApp.cpp
    AirfixXAudio2Backend.cpp
    AirfixXAudio2Backend.hpp
)

target_include_directories(airfix_windows PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
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
    airfix_windows_command_line
    airfix_windows_content_bootstrap
    airfix_windows_content_import
    airfix_windows_controller_profile_coordinator
    airfix_windows_input
    airfix_windows_renderer
    airfix_windows_render_settings_coordinator
    airfix_windows_settings_panel
    airfix_windows_settings_root
    airfix_windows_ui_automation
    airfix::audio
    airfix::content
    airfix::render
    airfix::runtime
    airfix::settings
    airfix::simulation
    ole32
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
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${airfix_lodepng_source_SOURCE_DIR}/LICENSE"
        "$<TARGET_FILE_DIR:airfix_windows>/third-party-licenses/LodePNG.txt"
    COMMENT "Copying the SDL3 and LodePNG zlib licenses"
    VERBATIM
)

if(AIRFIX_BUILD_TESTS)
    add_executable(airfix_windows_command_line_tests
        AirfixWindowsCommandLineTests.cpp)
    target_compile_features(airfix_windows_command_line_tests
        PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(airfix_windows_command_line_tests
            PRIVATE /W4 /permissive-)
    else()
        target_compile_options(airfix_windows_command_line_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
    target_link_libraries(airfix_windows_command_line_tests PRIVATE
        airfix_windows_command_line)
    add_test(NAME airfix_windows_command_line_tests
        COMMAND airfix_windows_command_line_tests)

    add_executable(airfix_windows_settings_root_tests
        AirfixWindowsSettingsRootTests.cpp)
    target_compile_features(airfix_windows_settings_root_tests
        PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(airfix_windows_settings_root_tests
            PRIVATE /W4 /permissive-)
    else()
        target_compile_options(airfix_windows_settings_root_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
    target_link_libraries(airfix_windows_settings_root_tests PRIVATE
        airfix_windows_settings_root)
    add_test(NAME airfix_windows_settings_root_tests
        COMMAND airfix_windows_settings_root_tests)

    add_executable(airfix_windows_content_import_tests
        AirfixWindowsContentImportTests.cpp)
    target_compile_features(airfix_windows_content_import_tests
        PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(airfix_windows_content_import_tests
            PRIVATE /W4 /permissive-)
    else()
        target_compile_options(airfix_windows_content_import_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
    target_link_libraries(airfix_windows_content_import_tests PRIVATE
        airfix_windows_content_import)
    add_test(NAME airfix_windows_content_import_tests
        COMMAND airfix_windows_content_import_tests)

    add_executable(airfix_windows_content_bootstrap_tests
        AirfixWindows.manifest
        AirfixWindowsContentBootstrapTests.cpp)
    target_compile_features(airfix_windows_content_bootstrap_tests
        PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(airfix_windows_content_bootstrap_tests
            PRIVATE /W4 /permissive-)
    else()
        target_compile_options(airfix_windows_content_bootstrap_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
    target_link_libraries(airfix_windows_content_bootstrap_tests PRIVATE
        airfix_windows_content_bootstrap)
    add_test(NAME airfix_windows_content_bootstrap_tests
        COMMAND airfix_windows_content_bootstrap_tests)

    add_executable(airfix_windows_input_mapping_tests
        AirfixSdlInputMappingTests.cpp)
    target_compile_features(airfix_windows_input_mapping_tests
        PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(airfix_windows_input_mapping_tests
            PRIVATE /W4 /permissive-)
    else()
        target_compile_options(airfix_windows_input_mapping_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
    target_link_libraries(airfix_windows_input_mapping_tests PRIVATE
        airfix_windows_input)
    add_test(NAME airfix_windows_input_mapping_tests
        COMMAND airfix_windows_input_mapping_tests)

    add_executable(airfix_windows_renderer_tests
        AirfixD3D11RendererTests.cpp)
    target_compile_features(airfix_windows_renderer_tests
        PRIVATE cxx_std_20)
    target_compile_definitions(airfix_windows_renderer_tests PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    if(MSVC)
        target_compile_options(airfix_windows_renderer_tests
            PRIVATE /W4 /permissive-)
    else()
        target_compile_options(airfix_windows_renderer_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
    target_link_libraries(airfix_windows_renderer_tests PRIVATE
        airfix_windows_renderer
        SDL3::SDL3-static
    )
    add_test(NAME airfix_windows_renderer_tests
        COMMAND airfix_windows_renderer_tests)
    set_tests_properties(airfix_windows_renderer_tests PROPERTIES
        TIMEOUT 60
    )

    add_executable(airfix_windows_render_settings_coordinator_tests
        AirfixWindowsRenderSettingsCoordinatorTests.cpp)
    target_compile_features(
        airfix_windows_render_settings_coordinator_tests
        PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(
            airfix_windows_render_settings_coordinator_tests PRIVATE
            /W4 /permissive-)
    else()
        target_compile_options(
            airfix_windows_render_settings_coordinator_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
    target_link_libraries(
        airfix_windows_render_settings_coordinator_tests PRIVATE
        airfix_windows_render_settings_coordinator)
    add_test(NAME airfix_windows_render_settings_coordinator_tests
        COMMAND airfix_windows_render_settings_coordinator_tests)

    add_executable(airfix_windows_controller_profile_coordinator_tests
        AirfixWindowsControllerProfileCoordinatorTests.cpp)
    target_compile_features(
        airfix_windows_controller_profile_coordinator_tests
        PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(
            airfix_windows_controller_profile_coordinator_tests PRIVATE
            /W4 /permissive-)
    else()
        target_compile_options(
            airfix_windows_controller_profile_coordinator_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
    target_link_libraries(
        airfix_windows_controller_profile_coordinator_tests PRIVATE
        airfix_windows_controller_profile_coordinator)
    add_test(NAME airfix_windows_controller_profile_coordinator_tests
        COMMAND airfix_windows_controller_profile_coordinator_tests)

    add_executable(airfix_windows_render_settings_panel_tests
        AirfixWindowsRenderSettingsPanelTests.cpp)
    target_compile_features(airfix_windows_render_settings_panel_tests
        PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(
            airfix_windows_render_settings_panel_tests PRIVATE
            /W4 /permissive-)
    else()
        target_compile_options(
            airfix_windows_render_settings_panel_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
    target_link_libraries(airfix_windows_render_settings_panel_tests PRIVATE
        airfix_windows_settings_panel)
    add_test(NAME airfix_windows_render_settings_panel_tests
        COMMAND airfix_windows_render_settings_panel_tests)

    add_executable(airfix_windows_ui_rasterizer_tests
        AirfixWindowsUiRasterizerTests.cpp)
    target_compile_features(airfix_windows_ui_rasterizer_tests
        PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(airfix_windows_ui_rasterizer_tests PRIVATE
            /W4 /permissive-)
    else()
        target_compile_options(airfix_windows_ui_rasterizer_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
    target_link_libraries(airfix_windows_ui_rasterizer_tests PRIVATE
        airfix_windows_ui_rasterizer)
    add_test(NAME airfix_windows_ui_rasterizer_tests
        COMMAND airfix_windows_ui_rasterizer_tests)

    add_executable(airfix_windows_ui_semantics_tests
        AirfixWindowsUiSemanticsTests.cpp)
    target_compile_features(airfix_windows_ui_semantics_tests
        PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(airfix_windows_ui_semantics_tests PRIVATE
            /W4 /permissive-)
    else()
        target_compile_options(airfix_windows_ui_semantics_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
    target_link_libraries(airfix_windows_ui_semantics_tests PRIVATE
        airfix_windows_ui_semantics)
    add_test(NAME airfix_windows_ui_semantics_tests
        COMMAND airfix_windows_ui_semantics_tests)

    add_executable(airfix_windows_ui_automation_tests
        AirfixWindowsUiAutomationTests.cpp)
    target_compile_features(airfix_windows_ui_automation_tests
        PRIVATE cxx_std_20)
    target_compile_definitions(airfix_windows_ui_automation_tests PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
    if(MSVC)
        target_compile_options(airfix_windows_ui_automation_tests PRIVATE
            /W4 /permissive-)
    else()
        target_compile_options(airfix_windows_ui_automation_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
    target_link_libraries(airfix_windows_ui_automation_tests PRIVATE
        airfix_windows_ui_automation
        ole32
        oleaut32
        uiautomationcore
        user32)
    add_test(NAME airfix_windows_ui_automation_tests
        COMMAND airfix_windows_ui_automation_tests)

    add_test(NAME airfix_windows_product_smoke_tests
        COMMAND airfix_windows --smoke-test --render-diagnostics)
    set_tests_properties(airfix_windows_product_smoke_tests PROPERTIES
        TIMEOUT 30
    )
    add_test(NAME airfix_windows_scaled_product_smoke_tests
        COMMAND airfix_windows
            --smoke-test
            --render-diagnostics
            --render-scale 50
            --original-4x3)
    set_tests_properties(
        airfix_windows_scaled_product_smoke_tests PROPERTIES
        TIMEOUT 30
    )
endif()
