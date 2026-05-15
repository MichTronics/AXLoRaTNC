#!/usr/bin/env python3
"""Maintain firmware/index.json: board catalogue + per-channel version info."""

import argparse
import datetime
import json
from pathlib import Path

BOARDS = [
    {"id": "devkitv1_e22",   "name": "ESP32 DevKit V1 + EBYTE E22",  "chip": "ESP32"},
    {"id": "heltec_v3",      "name": "Heltec WiFi LoRa 32 V3",       "chip": "ESP32-S3"},
    {"id": "tbeam",          "name": "TTGO T-Beam",                   "chip": "ESP32"},
    {"id": "lilygo_t3_v161", "name": "LilyGo T3 LoRa32 V1.6.1",     "chip": "ESP32"},
    {"id": "tbeam_supreme_433", "name": "LILYGO T-Beam SUPREME 433MHz", "chip": "ESP32-S3"},
]

# Manifest path template used by the webflasher ('{board}' is replaced client-side).
MANIFEST_PATHS = {
    "stable": "firmware/{board}/manifest.json",
    "dev":    "firmware/dev/{board}/manifest.json",
}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--index",   required=True,
                        help="Path to firmware/index.json to create or update")
    parser.add_argument("--channel", required=True, choices=["stable", "dev"])
    parser.add_argument("--version", required=True)
    parser.add_argument("--date",    default=datetime.date.today().isoformat())
    parser.add_argument("--url",     default="",
                        help="Release URL (stable) or commit URL (dev)")
    args = parser.parse_args()

    path = Path(args.index)
    idx: dict = json.loads(path.read_text()) if path.exists() else {}

    idx["boards"] = BOARDS
    idx.setdefault("channels", {})

    entry: dict = {
        "version":       args.version,
        "date":          args.date,
        "manifest_path": MANIFEST_PATHS[args.channel],
    }
    if args.channel == "stable":
        entry["release_url"] = args.url
    else:
        entry["commit_url"] = args.url

    idx["channels"][args.channel] = entry

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(idx, indent=2) + "\n")
    print(f"[ok] {path}: channel={args.channel} version={args.version}")


if __name__ == "__main__":
    main()
