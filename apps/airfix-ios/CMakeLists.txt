set(AIRFIX_IOS_BUNDLE_IDENTIFIER "com.tryk016.airfixdogfighter" CACHE STRING
    "Bundle identifier used by the unsigned iOS shell")

set(AIRFIX_IOS_INITIAL_SETUP_LOGICAL_PATH_BASE64 "" CACHE STRING
    "Optional Base64-encoded private setup logical path used by the iOS shell")
set(AIRFIX_IOS_INITIAL_LEVEL_LOGICAL_PATH_BASE64 "" CACHE STRING
    "Optional Base64-encoded private Level logical path used by the iOS shell")
set(AIRFIX_IOS_INITIAL_PLAYER_OBJECT_LOGICAL_PATH_BASE64 "" CACHE STRING
    "Optional Base64-encoded private player object logical path used by the iOS shell")
set(AIRFIX_IOS_INITIAL_START_INDEX "0" CACHE STRING
    "Optional initial mission start index used by the iOS shell")

foreach(_airfix_ios_base64_variable IN ITEMS
        AIRFIX_IOS_INITIAL_SETUP_LOGICAL_PATH_BASE64
        AIRFIX_IOS_INITIAL_LEVEL_LOGICAL_PATH_BASE64
        AIRFIX_IOS_INITIAL_PLAYER_OBJECT_LOGICAL_PATH_BASE64)
    set(_airfix_ios_base64_value "${${_airfix_ios_base64_variable}}")
    if(NOT _airfix_ios_base64_value MATCHES "^[A-Za-z0-9+/]*=?=?$")
        message(FATAL_ERROR
            "${_airfix_ios_base64_variable} must use only the Base64 alphabet "
            "(A-Z, a-z, 0-9, +, /, and up to two trailing = characters)")
    endif()

    string(LENGTH "${_airfix_ios_base64_value}" _airfix_ios_base64_length)
    math(EXPR _airfix_ios_base64_remainder
        "${_airfix_ios_base64_length} % 4")
    if(NOT _airfix_ios_base64_remainder EQUAL 0)
        message(FATAL_ERROR
            "${_airfix_ios_base64_variable} must be empty or have a Base64 "
            "length divisible by four")
    endif()
endforeach()

if(NOT AIRFIX_IOS_INITIAL_START_INDEX MATCHES "^[0-9]+$")
    message(FATAL_ERROR
        "AIRFIX_IOS_INITIAL_START_INDEX must be a decimal integer in the "
        "range 0..4294967295")
endif()

string(REGEX REPLACE "^0+" ""
    _airfix_ios_initial_start_index_normalized
    "${AIRFIX_IOS_INITIAL_START_INDEX}")
if(_airfix_ios_initial_start_index_normalized STREQUAL "")
    set(_airfix_ios_initial_start_index_normalized "0")
endif()
string(LENGTH "${_airfix_ios_initial_start_index_normalized}"
    _airfix_ios_initial_start_index_length)
if(_airfix_ios_initial_start_index_length GREATER 10 OR
        (_airfix_ios_initial_start_index_length EQUAL 10 AND
         _airfix_ios_initial_start_index_normalized STRGREATER "4294967295"))
    message(FATAL_ERROR
        "AIRFIX_IOS_INITIAL_START_INDEX must be a decimal integer in the "
        "range 0..4294967295")
endif()

configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/Info.plist.in"
    "${CMAKE_CURRENT_BINARY_DIR}/AirfixDogfighter-Info.plist"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/AirfixPrivateMissionConfig.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/AirfixPrivateMissionConfig.h"
    @ONLY
)

add_executable(airfix_ios MACOSX_BUNDLE
    "${CMAKE_CURRENT_BINARY_DIR}/AirfixPrivateMissionConfig.h"
    main.mm
    AirfixAppDelegate.h
    AirfixAppDelegate.mm
    AirfixAVAudioEngineBackend.hpp
    AirfixAVAudioEngineBackend.mm
    AirfixContentCoordinator.h
    AirfixContentCoordinator.mm
    AirfixMissionWorldRoomSnapshot.h
    AirfixMissionWorldRoomSnapshot.mm
    AirfixMissionWorldRoomSnapshot+Private.hpp
    AirfixGameControllerAdapter.h
    AirfixGameControllerAdapter.mm
    AirfixGameViewController.h
    AirfixGameViewController.mm
    AirfixIOSInputCoordinator.h
    AirfixIOSInputCoordinator.mm
    AirfixIOSInputCoordinator+Private.hpp
    AirfixIOSRenderSettingsStore.h
    AirfixIOSRenderSettingsStore.mm
    AirfixMetalRenderer.h
    AirfixMetalRenderer.mm
    AirfixRenderSettingsCoordinator.h
    AirfixRenderSettingsCoordinator.mm
    AirfixRenderSettingsPanelViewController.h
    AirfixRenderSettingsPanelViewController.mm
    AirfixShaders.metal
    AirfixTouchControlsView.h
    AirfixTouchControlsView.mm
)

# CMake does not place an otherwise unknown .metal extension in Xcode's Sources
# phase. Keep it visible in the project and compile it explicitly below.
set_source_files_properties(AirfixShaders.metal PROPERTIES HEADER_FILE_ONLY YES)

target_include_directories(airfix_ios PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
)
target_compile_features(airfix_ios PRIVATE cxx_std_20)
target_compile_options(airfix_ios PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror=unguarded-availability-new
)
target_link_libraries(airfix_ios PRIVATE
    airfix::afpack
    airfix::audio
    airfix::content
    airfix::input
    airfix::render
    airfix::runtime
    airfix::settings
    airfix::simulation
    "-framework AVFAudio"
    "-framework Foundation"
    "-framework UIKit"
    "-framework CoreGraphics"
    "-framework GameController"
    "-framework Metal"
    "-framework MetalKit"
    "-framework QuartzCore"
    "-framework UniformTypeIdentifiers"
)

find_program(AIRFIX_XCRUN_EXECUTABLE xcrun REQUIRED)
add_custom_command(TARGET airfix_ios POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
        "-DAIRFIX_XCRUN_EXECUTABLE:FILEPATH=${AIRFIX_XCRUN_EXECUTABLE}"
        "-DAIRFIX_METAL_SDK:STRING=${CMAKE_OSX_SYSROOT}"
        "-DAIRFIX_METAL_SOURCE:FILEPATH=${CMAKE_CURRENT_SOURCE_DIR}/AirfixShaders.metal"
        "-DAIRFIX_METAL_WORK_BASE:PATH=${CMAKE_CURRENT_BINARY_DIR}/metal"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/CompileMetal.cmake"
    COMMENT "Compiling offline Metal shader library"
    VERBATIM
)

set_target_properties(airfix_ios PROPERTIES
    OUTPUT_NAME "AirfixDogfighter"
    MACOSX_BUNDLE_INFO_PLIST
        "${CMAKE_CURRENT_BINARY_DIR}/AirfixDogfighter-Info.plist"
    XCODE_GENERATE_SCHEME YES
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
    XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET
        "${AIRFIX_IOS_DEPLOYMENT_TARGET}"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER
        "${AIRFIX_IOS_BUNDLE_IDENTIFIER}"
    XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphoneos iphonesimulator"
    XCODE_ATTRIBUTE_SUPPORTS_MACCATALYST NO
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1"
)
