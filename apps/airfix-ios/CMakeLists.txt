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
)

target_compile_features(airfix_ios PRIVATE cxx_std_20)
target_compile_options(airfix_ios PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror=unguarded-availability-new
)
target_link_libraries(airfix_ios PRIVATE
    airfix::runtime
    "-framework UIKit"
    "-framework CoreGraphics"
    "-framework Metal"
    "-framework MetalKit"
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
