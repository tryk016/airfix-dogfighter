#!/usr/bin/env python3
"""Validate an Ad Hoc provisioning profile without exposing private values."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import plistlib
import re
import sys


MAX_PROFILE_PLIST_BYTES = 2 * 1024 * 1024
TEAM_ID_PATTERN = re.compile(r"^[A-Z0-9]{10}$")
BUNDLE_ID_PATTERN = re.compile(
    r"^[A-Za-z0-9](?:[A-Za-z0-9.-]{1,253}[A-Za-z0-9])?$"
)
PROFILE_UUID_PATTERN = re.compile(
    r"^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-"
    r"[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$"
)
DEVICE_UDID_PATTERN = re.compile(r"^[0-9A-Fa-f-]{20,64}$")


class ProfileValidationFailure(RuntimeError):
    pass


def require_string(value: object, field: str) -> str:
    if not isinstance(value, str) or not value or any(
        ord(character) < 0x20 for character in value
    ):
        raise ProfileValidationFailure(f"invalid {field}")
    return value


def require_string_list(value: object, field: str) -> list[str]:
    if not isinstance(value, list) or not value:
        raise ProfileValidationFailure(f"invalid {field}")
    result: list[str] = []
    for entry in value:
        result.append(require_string(entry, field))
    return result


def normalize_expiration(value: object) -> datetime:
    if not isinstance(value, datetime):
        raise ProfileValidationFailure("invalid profile expiration")
    if value.tzinfo is None:
        return value.replace(tzinfo=timezone.utc)
    return value.astimezone(timezone.utc)


def validate_expected_inputs(
    team_id: str,
    bundle_id: str,
    profile_name: str,
    device_udids: list[str],
) -> None:
    if TEAM_ID_PATTERN.fullmatch(team_id) is None:
        raise ProfileValidationFailure("invalid expected team identifier")
    if (
        BUNDLE_ID_PATTERN.fullmatch(bundle_id) is None
        or ".." in bundle_id
        or "*" in bundle_id
    ):
        raise ProfileValidationFailure("invalid expected bundle identifier")
    if not profile_name or len(profile_name) > 128 or any(
        ord(character) < 0x20 for character in profile_name
    ):
        raise ProfileValidationFailure("invalid expected profile name")
    if len(device_udids) != 2 or len(set(device_udids)) != 2:
        raise ProfileValidationFailure("exactly two distinct device identifiers are required")
    if any(DEVICE_UDID_PATTERN.fullmatch(value) is None for value in device_udids):
        raise ProfileValidationFailure("invalid expected device identifier")


def validate_profile_document(
    document: object,
    *,
    team_id: str,
    bundle_id: str,
    profile_name: str,
    device_udids: list[str],
    now: datetime | None = None,
) -> dict[str, object]:
    validate_expected_inputs(team_id, bundle_id, profile_name, device_udids)
    if not isinstance(document, dict):
        raise ProfileValidationFailure("profile root must be a dictionary")

    profile_uuid = require_string(document.get("UUID"), "profile UUID")
    if PROFILE_UUID_PATTERN.fullmatch(profile_uuid) is None:
        raise ProfileValidationFailure("invalid profile UUID")
    if require_string(document.get("Name"), "profile name") != profile_name:
        raise ProfileValidationFailure("profile name does not match configuration")

    team_identifiers = require_string_list(
        document.get("TeamIdentifier"), "team identifier list"
    )
    prefixes = require_string_list(
        document.get("ApplicationIdentifierPrefix"),
        "application identifier prefix",
    )
    if team_id not in team_identifiers or team_id not in prefixes:
        raise ProfileValidationFailure("profile team does not match configuration")

    expiration = normalize_expiration(document.get("ExpirationDate"))
    comparison_time = now or datetime.now(timezone.utc)
    if comparison_time.tzinfo is None:
        comparison_time = comparison_time.replace(tzinfo=timezone.utc)
    if expiration <= comparison_time.astimezone(timezone.utc):
        raise ProfileValidationFailure("provisioning profile is expired")

    entitlements = document.get("Entitlements")
    if not isinstance(entitlements, dict):
        raise ProfileValidationFailure("profile entitlements are missing")
    expected_application_identifier = f"{team_id}.{bundle_id}"
    if entitlements.get("application-identifier") != expected_application_identifier:
        raise ProfileValidationFailure("profile application identifier does not match")
    if entitlements.get("com.apple.developer.team-identifier") != team_id:
        raise ProfileValidationFailure("profile entitlement team does not match")
    if entitlements.get("get-task-allow") is not False:
        raise ProfileValidationFailure("profile is not an Ad Hoc distribution profile")

    if document.get("ProvisionsAllDevices") not in (None, False):
        raise ProfileValidationFailure("enterprise provisioning is not accepted")
    provisioned_devices = require_string_list(
        document.get("ProvisionedDevices"), "provisioned device list"
    )
    if any(device not in provisioned_devices for device in device_udids):
        raise ProfileValidationFailure("profile does not contain both expected devices")

    certificates = document.get("DeveloperCertificates")
    if (
        not isinstance(certificates, list)
        or not certificates
        or any(not isinstance(certificate, bytes) or not certificate for certificate in certificates)
    ):
        raise ProfileValidationFailure("profile has no distribution certificate")

    return {
        "schema": "airfix.ios-provisioning-summary",
        "version": 1,
        "profileUuid": profile_uuid,
        "expirationUtc": expiration.isoformat().replace("+00:00", "Z"),
        "provisionedDeviceCount": len(provisioned_devices),
    }


def load_profile_plist(path: Path) -> object:
    try:
        if not path.is_file() or path.stat().st_size > MAX_PROFILE_PLIST_BYTES:
            raise ProfileValidationFailure("profile plist is missing or oversized")
        return plistlib.loads(path.read_bytes())
    except ProfileValidationFailure:
        raise
    except (OSError, plistlib.InvalidFileException, ValueError) as error:
        raise ProfileValidationFailure("profile plist is malformed") from error


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile-plist", required=True, type=Path)
    parser.add_argument("--team-id", required=True)
    parser.add_argument("--bundle-id", required=True)
    parser.add_argument("--profile-name", required=True)
    parser.add_argument("--device-udid", required=True, action="append")
    parser.add_argument("--summary-output", type=Path)
    args = parser.parse_args()
    try:
        summary = validate_profile_document(
            load_profile_plist(args.profile_plist),
            team_id=args.team_id,
            bundle_id=args.bundle_id,
            profile_name=args.profile_name,
            device_udids=args.device_udid,
        )
        if args.summary_output is not None:
            args.summary_output.parent.mkdir(parents=True, exist_ok=True)
            args.summary_output.write_text(
                json.dumps(summary, sort_keys=True, indent=2) + "\n",
                encoding="utf-8",
            )
        print("validated Ad Hoc provisioning profile for two expected devices")
        return 0
    except ProfileValidationFailure as error:
        print(f"iOS provisioning validation failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
