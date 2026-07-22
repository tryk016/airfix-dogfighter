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

# The shader is compiled explicitly below. Keeping it in the target as a
# header-only source makes source changes visible to the Xcode generator
# without adding a runtime shader compiler path.
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
set(AIRFIX_METAL_WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/metal/$<CONFIG>")
add_custom_command(TARGET airfix_ios POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${AIRFIX_METAL_WORK_DIR}"
    COMMAND "${AIRFIX_XCRUN_EXECUTABLE}" -sdk "${CMAKE_OSX_SYSROOT}"
        metal -c "${CMAKE_CURRENT_SOURCE_DIR}/AirfixShaders.metal"
        -o "${AIRFIX_METAL_WORK_DIR}/AirfixShaders.air"
    COMMAND "${AIRFIX_XCRUN_EXECUTABLE}" -sdk "${CMAKE_OSX_SYSROOT}"
        metallib "${AIRFIX_METAL_WORK_DIR}/AirfixShaders.air"
        -o "$<TARGET_BUNDLE_DIR:airfix_ios>/default.metallib"
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
