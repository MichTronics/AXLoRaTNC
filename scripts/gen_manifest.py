#!/usr/bin/env python3
"""Generate ESP Web Tools manifest.json for each AXLoRaTNC firmware variant."""

import argparse
import json
import shutil
from pathlib import Path

# Variant metadata: chip family and bootloader flash offset.
# ESP32 classic → bootloader at 0x1000; ESP32-S3/C3 → bootloader at 0x0.
VARIANTS = {
    "devkitv1_e22": {
        "chip": "ESP32",
        "name": "ESP32 DevKit V1 + EBYTE E22 (SX1262)",
        "bootloader_offset": 0x1000,
    },
    "heltec_v3": {
        "chip": "ESP32-S3",
        "name": "Heltec WiFi LoRa 32 V3",
        "bootloader_offset": 0x0,
    },
    "tbeam": {
        "chip": "ESP32",
        "name": "TTGO T-Beam (SX1276)",
        "bootloader_offset": 0x1000,
    },
    "lilygo_t3_v161": {
        "chip": "ESP32",
        "name": "LilyGo T3 LoRa32 V1.6.1",
        "bootloader_offset": 0x1000,
    },
    "tbeam_supreme_433": {
        "chip": "ESP32-S3",
        "name": "LILYGO T-Beam SUPREME 433MHz (SX1262)",
        "bootloader_offset": 0x0,
    },
}


def build_manifest(env: str, info: dict, version: str, fw_dir: Path) -> dict:
    parts = []
    bootloader = fw_dir / "bootloader.bin"
    partitions = fw_dir / "partitions.bin"
    firmware   = fw_dir / "firmware.bin"

    if bootloader.exists():
        parts.append({"path": "bootloader.bin", "offset": info["bootloader_offset"]})
    if partitions.exists():
        parts.append({"path": "partitions.bin", "offset": 0x8000})
    if firmware.exists():
        parts.append({"path": "firmware.bin",   "offset": 0x10000})

    if not parts:
        raise FileNotFoundError(f"No .bin files found in {fw_dir}")

    return {
        "name": f"AXLoRaTNC – {info['name']}",
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [{"chipFamily": info["chip"], "parts": parts}],
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifacts", required=True,
                        help="Directory containing firmware-<env>/ subdirs")
    parser.add_argument("--output",    required=True,
                        help="Output root (e.g. docs/firmware/)")
    parser.add_argument("--version",   required=True,
                        help="Version string embedded in manifests")
    args = parser.parse_args()

    artifacts = Path(args.artifacts)
    output    = Path(args.output)

    for env, info in VARIANTS.items():
        src = artifacts / f"firmware-{env}"
        if not src.exists():
            print(f"[skip] {env}: no artifact directory found")
            continue

        dst = output / env
        dst.mkdir(parents=True, exist_ok=True)

        for f in src.glob("*.bin"):
            shutil.copy2(f, dst / f.name)

        manifest = build_manifest(env, info, args.version, dst)
        (dst / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        print(f"[ok]   {env} ({info['chip']}) → {dst}/manifest.json")


if __name__ == "__main__":
    main()
