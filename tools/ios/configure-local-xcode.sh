#!/bin/bash
set -euo pipefail

usage() {
    echo "usage: configure-local-xcode.sh --source-sha SHA --team-id TEAMID [--bundle-id ID] [--build-root DIR]" >&2
}

source_sha=""
team_id=""
bundle_id="com.tryk016.airfixdogfighter"
build_root=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --source-sha)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            source_sha="$2"
            shift 2
            ;;
        --team-id)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            team_id="$2"
            shift 2
            ;;
        --bundle-id)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            bundle_id="$2"
            shift 2
            ;;
        --build-root)
            [[ $# -ge 2 ]] || { usage; exit 2; }
            build_root="$2"
            shift 2
            ;;
        *)
            usage
            exit 2
            ;;
    esac
done

if [[ ! "$source_sha" =~ ^[0-9a-f]{40}$ ]]; then
    echo "source SHA must contain exactly 40 lowercase hexadecimal characters" >&2
    exit 2
fi
if [[ ! "$team_id" =~ ^[A-Za-z0-9]{10}$ ]]; then
    echo "team ID must contain exactly 10 ASCII letters or digits" >&2
    exit 2
fi
if [[ ${#bundle_id} -gt 255 ]] ||
    [[ ! "$bundle_id" =~ ^[A-Za-z0-9]+([.-][A-Za-z0-9]+)+$ ]]; then
    echo "bundle identifier is invalid" >&2
    exit 2
fi
if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "local Xcode configuration requires macOS" >&2
    exit 1
fi

script_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
repository_root="$(cd -- "$script_root/../.." && pwd -P)"
actual_source_sha="$(git -C "$repository_root" rev-parse HEAD)"
if [[ "$actual_source_sha" != "$source_sha" ]]; then
    echo "checked-out revision does not match the requested source SHA" >&2
    exit 1
fi
if [[ -n "$(git -C "$repository_root" status --porcelain --untracked-files=no)" ]]; then
    echo "tracked source changes must be committed or removed before device build" >&2
    exit 1
fi
if [[ -z "$build_root" ]]; then
    build_root="$repository_root/build-ios-device-local"
fi

developer_root="${DEVELOPER_DIR:-$(xcode-select -p)}"
if [[ ! -x "$developer_root/usr/bin/xcodebuild" ]]; then
    echo "active Xcode developer directory is unavailable" >&2
    exit 1
fi

cmake -S "$repository_root" -B "$build_root" -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT=iphoneos \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=16.4 \
    -DAIRFIX_IOS_DEPLOYMENT_TARGET=16.4 \
    -DAIRFIX_BUILD_TESTS=OFF \
    -DAIRFIX_BUILD_TOOLS=OFF \
    -DAIRFIX_BUILD_IOS_APP=ON \
    -DAIRFIX_IOS_SIMULATOR_SMOKE=OFF \
    -DAIRFIX_IOS_ENABLE_LOCAL_SIGNING=ON \
    -DAIRFIX_IOS_LOCAL_DEVELOPMENT_TEAM="$team_id" \
    -DAIRFIX_IOS_BUNDLE_IDENTIFIER="$bundle_id" \
    -DAIRFIX_IOS_INITIAL_SETUP_LOGICAL_PATH_BASE64= \
    -DAIRFIX_IOS_INITIAL_LEVEL_LOGICAL_PATH_BASE64= \
    -DAIRFIX_IOS_INITIAL_PLAYER_OBJECT_LOGICAL_PATH_BASE64= \
    -DAIRFIX_IOS_INITIAL_START_INDEX=0

project_count="$(find "$build_root" -maxdepth 1 -type d -name '*.xcodeproj' -print | wc -l | tr -d ' ')"
if [[ "$project_count" != "1" ]]; then
    echo "expected exactly one generated Xcode project" >&2
    exit 1
fi
project="$(find "$build_root" -maxdepth 1 -type d -name '*.xcodeproj' -print | sed -n '1p')"

echo "local data-less Xcode project is ready"
echo "project: $project"
echo "next: open the project, select the airfix_ios scheme and a connected iPhone, then choose Product > Run"
