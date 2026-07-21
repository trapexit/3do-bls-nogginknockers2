#!/usr/bin/env python3
"""Extract the preserved non-CD music streams from the retail VOL files."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any, Dict, List

NOG2VOL_DIRECTORY = Path(__file__).resolve().parents[1] / "nog2vol"
sys.path.insert(0, str(NOG2VOL_DIRECTORY))

from formats import FormatError, parse_path  # noqa: E402


TRACKS = (
    ("NOGTITLE.VOL", "scene.music", "title-midi.bin", "MIDI"),
    (
        "NOGTITLE.VOL",
        "scene.awe32_music",
        "title-awe32.bin",
        "MIDI",
    ),
    ("NOGGINBG.VOL", "scene.music", "match-cmf.bin", "CMF"),
)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def extract_track(
    source_directory: Path,
    output_directory: Path,
    volume_name: str,
    role: str,
    output_name: str,
    expected_format: str,
) -> Dict[str, Any]:
    source_path = source_directory / volume_name
    source = source_path.read_bytes()
    parsed = parse_path(source_path)
    records = [
        record for record in parsed["records"] if record["role"] == role
    ]
    if len(records) != 1:
        raise FormatError(
            "%s has %d records named %s"
            % (volume_name, len(records), role)
        )
    record = records[0]
    if record.get("metadata", {}).get("format") != expected_format:
        raise FormatError(
            "%s %s is not %s" % (volume_name, role, expected_format)
        )
    offset = int(record["offset"])
    size = int(record["size"])
    payload = source[offset : offset + size]
    if len(payload) != size or sha256_bytes(payload) != record["sha256"]:
        raise FormatError("%s %s extraction does not match its parse" %
                          (volume_name, role))
    output_path = output_directory / output_name
    output_path.write_bytes(payload)
    return {
        "format": expected_format,
        "output": output_name,
        "role": role,
        "sha256": sha256_bytes(payload),
        "size": size,
        "source": volume_name,
        "source_offset": offset,
        "source_sha256": parsed["sha256"],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_directory", type=Path)
    parser.add_argument("output_directory", type=Path)
    args = parser.parse_args()

    try:
        args.output_directory.mkdir(parents=True, exist_ok=True)
        tracks: List[Dict[str, Any]] = []
        for track in TRACKS:
            tracks.append(
                extract_track(
                    args.source_directory,
                    args.output_directory,
                    *track,
                )
            )
        timbres_path = args.source_directory / "TIMBRES.VOL"
        timbres = timbres_path.read_bytes()
        parsed_timbres = parse_path(timbres_path)
        (args.output_directory / "timbres.vol").write_bytes(timbres)
        manifest = {
            "format": "nog2-music-source-v1",
            "generated_by": "tools/music/extract_music.py",
            "timbres": {
                "output": "timbres.vol",
                "patch_count": parsed_timbres["summary"]["patch_count"],
                "sha256": sha256_bytes(timbres),
                "size": len(timbres),
                "source": "TIMBRES.VOL",
            },
            "tracks": tracks,
        }
        (args.output_directory / "manifest.json").write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    except (FormatError, OSError, ValueError) as error:
        print("extract_music.py: %s" % error, file=sys.stderr)
        return 1

    print(
        "extracted %d music streams and %d OPL2 patches"
        % (len(tracks), parsed_timbres["summary"]["patch_count"])
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
