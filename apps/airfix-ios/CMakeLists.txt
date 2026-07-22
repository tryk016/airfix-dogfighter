set(AIRFIX_IOS_BUNDLE_IDENTIFIER "com.tryk016.airfixdogfighter" CACHE STRING
    "Bundle identifier used by the unsigned iOS shell")

configure_file(
    Info.plist.in
    "${CMAKE_CURRENT_BINARY_DIR}/AirfixDogfighter-Info.plist"
    @ONLY
)

add_executable(airfix_ios MACOSX_BUNDLE
    main.mm
    AirfixAppDelegate.h
    AirfixAppDelegate.mm
    AirfixGameViewController.h
    AirfixGameViewController.mm
    AirfixMetalRenderer.h
    AirfixMetalRenderer.mm
    AirfixShaders.metal
)

# CMake does not place an otherwise unknown .metal extension in Xcode's Sources
# phase. Keep it visible in the project and compile it explicitly below.
set_source_files_properties(AirfixShaders.metal PROPERTIES HEADER_FILE_ONLY YES)

target_compile_features(airfix_ios PRIVATE cxx_std_20)
target_compile_options(airfix_ios PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror=unguarded-availability-new
)
target_link_libraries(airfix_ios PRIVATE
    airfix::render
    airfix::runtime
    "-framework UIKit"
    "-framework CoreGraphics"
    "-framework Metal"
    "-framework MetalKit"
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
