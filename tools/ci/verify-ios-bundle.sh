#!/bin/bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: verify-ios-bundle.sh <build-root> <iphoneos|iphonesimulator>" >&2
    exit 2
fi

build_root="$1"
sdk="$2"
case "$sdk" in
    iphoneos)
        expected_platform="IOS"
        ;;
    iphonesimulator)
        expected_platform="IOSSIMULATOR"
        ;;
    *)
        echo "unsupported SDK: $sdk" >&2
        exit 2
        ;;
esac

bundles="$(find "$build_root" -type d -name AirfixDogfighter.app -print)"
bundle_count="$(printf '%s\n' "$bundles" | sed '/^$/d' | wc -l | tr -d ' ')"
if [[ "$bundle_count" != "1" ]]; then
    echo "expected exactly one AirfixDogfighter.app, found $bundle_count" >&2
    exit 1
fi
bundle="$(printf '%s\n' "$bundles" | sed -n '1p')"
plist="$bundle/Info.plist"

minimum_os="$(plutil -extract MinimumOSVersion raw -o - "$plist")"
if [[ "$minimum_os" != "16.4" ]]; then
    echo "unexpected MinimumOSVersion: $minimum_os" >&2
    exit 1
fi

executable_name="$(plutil -extract CFBundleExecutable raw -o - "$plist")"
executable="$bundle/$executable_name"
if [[ ! -f "$executable" ]]; then
    echo "bundle executable is missing: $executable" >&2
    exit 1
fi

shader_library="$bundle/default.metallib"
if [[ ! -s "$shader_library" ]]; then
    echo "offline Metal shader library is missing or empty: $shader_library" >&2
    exit 1
fi

lodepng_license="$bundle/third-party-licenses/LodePNG.txt"
if [[ ! -s "$lodepng_license" ]]; then
    echo "LodePNG license is missing or empty: $lodepng_license" >&2
    exit 1
fi

architectures="$(lipo -archs "$executable")"
if [[ " $architectures " != *" arm64 "* ]]; then
    echo "bundle has no ARM64 slice: $architectures" >&2
    exit 1
fi

build_version="$(xcrun vtool -show-build "$executable")"
if ! grep -Eq "platform +${expected_platform}" <<<"$build_version"; then
    echo "binary platform does not match $sdk" >&2
    printf '%s\n' "$build_version" >&2
    exit 1
fi
if ! grep -Eq 'minos +16\.4(\.0)?' <<<"$build_version"; then
    echo "binary minimum OS is not 16.4" >&2
    printf '%s\n' "$build_version" >&2
    exit 1
fi

forbidden="$(find "$bundle" -type f \( \
    -iname '*.afpack' -o -iname '*.up' -o -iname '*.gti' -o \
    -iname '*.ccf' -o -iname '*.exe' -o -iname '*.dll' -o \
    -iname '*.icd' -o -iname '*.mode' -o -iname '*.type' -o \
    -iname '*.p12' -o -iname '*.mobileprovision' -o -iname '*.ipa' \
    \) -print)"
if [[ -n "$forbidden" ]]; then
    echo "forbidden private files found in application bundle:" >&2
    printf '%s\n' "$forbidden" >&2
    exit 1
fi

if grep -R -a -E -i -q \
    '(([A-Za-z]:[\\/]|/Users/[^/]+/|/home/[^/]+/)[^[:cntrl:]]*roms[\\/]|/roms/)' \
    "$bundle"; then
    echo "private source-library path leaked into application bundle" >&2
    exit 1
fi

echo "verified iOS bundle: sdk=$sdk minOS=$minimum_os archs=$architectures"
