#!/bin/bash
set -euo pipefail

umask 077

required_variables=(
    AIRFIX_REPOSITORY_PRIVATE
    AIRFIX_IOS_TEAM_ID
    AIRFIX_IOS_BUNDLE_IDENTIFIER
    AIRFIX_IOS_PROFILE_NAME
    AIRFIX_IOS_SIGNING_IDENTITY
    AIRFIX_IOS_DEVICE_UDID_PRIMARY
    AIRFIX_IOS_DEVICE_UDID_SECONDARY
    BUILD_CERTIFICATE_BASE64
    P12_PASSWORD
    BUILD_PROVISION_PROFILE_BASE64
    KEYCHAIN_PASSWORD
    GITHUB_SHA
    GITHUB_RUN_ID
    ImageOS
    ImageVersion
    RUNNER_TEMP
)
for variable_name in "${required_variables[@]}"; do
    if [[ -z "${!variable_name:-}" ]]; then
        echo "missing required private signing input: $variable_name" >&2
        exit 1
    fi
done

if [[ "$AIRFIX_REPOSITORY_PRIVATE" != "true" ]]; then
    echo "private signing is disabled outside a private repository" >&2
    exit 1
fi
if [[ "${GITHUB_REF:-}" != "refs/heads/main" ]]; then
    echo "private signing is restricted to the trusted main branch" >&2
    exit 1
fi
if [[ ! "$GITHUB_SHA" =~ ^[0-9a-f]{40}$ ]] ||
    [[ ! "$GITHUB_RUN_ID" =~ ^[1-9][0-9]*$ ]]; then
    echo "invalid trusted workflow identity" >&2
    exit 1
fi
if [[ "$AIRFIX_IOS_SIGNING_IDENTITY" != "Apple Distribution" ]]; then
    echo "Ad Hoc export requires the Apple Distribution signing identity" >&2
    exit 1
fi

initial_setup="${AIRFIX_IOS_INITIAL_SETUP_LOGICAL_PATH_BASE64:-}"
initial_level="${AIRFIX_IOS_INITIAL_LEVEL_LOGICAL_PATH_BASE64:-}"
initial_player="${AIRFIX_IOS_INITIAL_PLAYER_OBJECT_LOGICAL_PATH_BASE64:-}"
initial_start="${AIRFIX_IOS_INITIAL_START_INDEX:-0}"
if [[ -n "$initial_setup" && -z "$initial_level" ]] ||
    [[ -z "$initial_setup" && -n "$initial_level" ]]; then
    echo "private mission configuration requires setup and Level together" >&2
    exit 1
fi
if [[ -n "$initial_player" && -z "$initial_setup" ]]; then
    echo "private player configuration requires setup and Level" >&2
    exit 1
fi
if [[ ! "$initial_start" =~ ^[0-9]{1,10}$ ]]; then
    echo "invalid private mission start index" >&2
    exit 1
fi

work_root="$RUNNER_TEMP/airfix-ios-private-$GITHUB_RUN_ID"
certificate_path="$work_root/signing-certificate.p12"
profile_path="$work_root/signing-profile.mobileprovision"
profile_plist="$work_root/signing-profile.plist"
profile_summary="$work_root/signing-profile-summary.json"
keychain_path="$work_root/airfix-signing.keychain-db"
build_root="$work_root/build-iphoneos"
archive_path="$work_root/AirfixDogfighter.xcarchive"
export_root="$work_root/export"
export_options="$work_root/ExportOptions.plist"
output_root="$RUNNER_TEMP/airfix-private-output-$GITHUB_RUN_ID"
installed_profile=""

cleanup() {
    set +e
    if [[ -n "$installed_profile" ]]; then
        rm -f -- "$installed_profile"
    fi
    security delete-keychain "$keychain_path" >/dev/null 2>&1
    rm -rf -- "$work_root"
}
trap cleanup EXIT INT TERM

rm -rf -- "$work_root" "$output_root"
mkdir -p -- "$work_root" "$output_root"

printf '%s' "$BUILD_CERTIFICATE_BASE64" | base64 --decode >"$certificate_path"
printf '%s' "$BUILD_PROVISION_PROFILE_BASE64" | base64 --decode >"$profile_path"
if [[ ! -s "$certificate_path" || ! -s "$profile_path" ]]; then
    echo "decoded signing material is empty" >&2
    exit 1
fi

security cms -D -i "$profile_path" >"$profile_plist"
python3 tools/ci/validate_ios_provisioning_profile.py \
    --profile-plist "$profile_plist" \
    --team-id "$AIRFIX_IOS_TEAM_ID" \
    --bundle-id "$AIRFIX_IOS_BUNDLE_IDENTIFIER" \
    --profile-name "$AIRFIX_IOS_PROFILE_NAME" \
    --device-udid "$AIRFIX_IOS_DEVICE_UDID_PRIMARY" \
    --device-udid "$AIRFIX_IOS_DEVICE_UDID_SECONDARY" \
    --summary-output "$profile_summary"

profile_uuid="$(plutil -extract UUID raw -o - "$profile_plist")"
if [[ ! "$profile_uuid" =~ ^[0-9A-Fa-f-]{36}$ ]]; then
    echo "decoded profile has an invalid UUID" >&2
    exit 1
fi
profiles_root="$HOME/Library/MobileDevice/Provisioning Profiles"
mkdir -p -- "$profiles_root"
installed_profile="$profiles_root/$profile_uuid.mobileprovision"
cp -- "$profile_path" "$installed_profile"

security create-keychain -p "$KEYCHAIN_PASSWORD" "$keychain_path"
security set-keychain-settings -lut 21600 "$keychain_path"
security unlock-keychain -p "$KEYCHAIN_PASSWORD" "$keychain_path"
security import "$certificate_path" -P "$P12_PASSWORD" -A -t cert -f pkcs12 \
    -k "$keychain_path" >/dev/null
security set-key-partition-list -S apple-tool:,apple: -k "$KEYCHAIN_PASSWORD" \
    "$keychain_path" >/dev/null
security list-keychains -d user -s "$keychain_path"
security default-keychain -d user -s "$keychain_path"
identity_count="$(security find-identity -v -p codesigning "$keychain_path" | \
    grep -Ec '^[[:space:]]*[0-9]+\)' || true)"
if [[ "$identity_count" != "1" ]]; then
    echo "temporary keychain must contain exactly one valid signing identity" >&2
    exit 1
fi

cmake -S . -B "$build_root" -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT=iphoneos \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=16.4 \
    -DAIRFIX_IOS_DEPLOYMENT_TARGET=16.4 \
    -DAIRFIX_BUILD_TESTS=OFF \
    -DAIRFIX_BUILD_TOOLS=OFF \
    -DAIRFIX_BUILD_IOS_APP=ON \
    -DAIRFIX_IOS_ENABLE_SIGNING=ON \
    -DAIRFIX_IOS_SIMULATOR_SMOKE=OFF \
    -DAIRFIX_IOS_BUNDLE_IDENTIFIER="$AIRFIX_IOS_BUNDLE_IDENTIFIER" \
    -DAIRFIX_IOS_INITIAL_SETUP_LOGICAL_PATH_BASE64="$initial_setup" \
    -DAIRFIX_IOS_INITIAL_LEVEL_LOGICAL_PATH_BASE64="$initial_level" \
    -DAIRFIX_IOS_INITIAL_PLAYER_OBJECT_LOGICAL_PATH_BASE64="$initial_player" \
    -DAIRFIX_IOS_INITIAL_START_INDEX="$initial_start"

xcode_project_count="$(find "$build_root" -maxdepth 1 -type d -name '*.xcodeproj' \
    -print | wc -l | tr -d ' ')"
if [[ "$xcode_project_count" != "1" ]]; then
    echo "expected exactly one generated Xcode project" >&2
    exit 1
fi
xcode_project="$(find "$build_root" -maxdepth 1 -type d -name '*.xcodeproj' \
    -print | sed -n '1p')"

xcodebuild -quiet \
    -project "$xcode_project" \
    -scheme airfix_ios \
    -configuration Release \
    -sdk iphoneos \
    -archivePath "$archive_path" \
    archive \
    CODE_SIGNING_ALLOWED=YES \
    CODE_SIGNING_REQUIRED=YES \
    CODE_SIGN_STYLE=Manual \
    DEVELOPMENT_TEAM="$AIRFIX_IOS_TEAM_ID" \
    PRODUCT_BUNDLE_IDENTIFIER="$AIRFIX_IOS_BUNDLE_IDENTIFIER" \
    CODE_SIGN_IDENTITY="$AIRFIX_IOS_SIGNING_IDENTITY" \
    PROVISIONING_PROFILE_SPECIFIER="$AIRFIX_IOS_PROFILE_NAME" \
    OTHER_CODE_SIGN_FLAGS="--keychain $keychain_path"

python3 - "$export_options" "$AIRFIX_IOS_TEAM_ID" \
    "$AIRFIX_IOS_BUNDLE_IDENTIFIER" "$AIRFIX_IOS_PROFILE_NAME" \
    "$AIRFIX_IOS_SIGNING_IDENTITY" <<'PY'
import plistlib
import sys

output, team_id, bundle_id, profile_name, signing_identity = sys.argv[1:]
document = {
    "destination": "export",
    "method": "ad-hoc",
    "provisioningProfiles": {bundle_id: profile_name},
    "signingCertificate": signing_identity,
    "signingStyle": "manual",
    "stripSwiftSymbols": False,
    "teamID": team_id,
    "thinning": "<none>",
}
with open(output, "wb") as stream:
    plistlib.dump(document, stream, sort_keys=True)
PY

xcodebuild -quiet -exportArchive \
    -archivePath "$archive_path" \
    -exportPath "$export_root" \
    -exportOptionsPlist "$export_options"

ipa_count="$(find "$export_root" -maxdepth 1 -type f -name '*.ipa' -print | \
    wc -l | tr -d ' ')"
if [[ "$ipa_count" != "1" ]]; then
    echo "expected exactly one exported IPA" >&2
    exit 1
fi
exported_ipa="$(find "$export_root" -maxdepth 1 -type f -name '*.ipa' -print | \
    sed -n '1p')"
final_ipa="$output_root/AirfixDogfighter-$GITHUB_SHA.ipa"
final_manifest="$output_root/build-manifest.json"
cp -- "$exported_ipa" "$final_ipa"

python3 tools/ci/verify_ios_signed_ipa.py \
    --ipa "$final_ipa" \
    --manifest-output "$final_manifest" \
    --team-id "$AIRFIX_IOS_TEAM_ID" \
    --bundle-id "$AIRFIX_IOS_BUNDLE_IDENTIFIER" \
    --profile-name "$AIRFIX_IOS_PROFILE_NAME" \
    --device-udid "$AIRFIX_IOS_DEVICE_UDID_PRIMARY" \
    --device-udid "$AIRFIX_IOS_DEVICE_UDID_SECONDARY" \
    --source-sha "$GITHUB_SHA" \
    --workflow-run-id "$GITHUB_RUN_ID" \
    --runner-image "$ImageOS/$ImageVersion" \
    --dependency-lock docs/toolchain/LOCK.md

chmod 600 "$final_ipa" "$final_manifest"
echo "private signed IPA and manifest are ready for protected upload"
