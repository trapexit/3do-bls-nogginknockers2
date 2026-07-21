#!/usr/bin/env python3
"""Inventory and verify source-derived Noggin Knockers 2 VOL files."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Sequence

from assets import extract_release
from formats import EXPECTED_FILES, FormatError, parse_path


def serialized(value: Any) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")


def build_inventory(input_directory: Path) -> Dict[str, Any]:
    if not input_directory.is_dir():
        raise FormatError("not a directory: %s" % input_directory)
    paths = sorted(input_directory.glob("*.VOL"), key=lambda path: path.name.casefold())
    names = {path.name.upper() for path in paths}
    missing = sorted(EXPECTED_FILES - names)
    extra = sorted(names - EXPECTED_FILES)
    if missing or extra:
        raise FormatError("VOL set mismatch: missing=%r extra=%r" % (missing, extra))

    files: List[Dict[str, Any]] = []
    for path in paths:
        try:
            parsed = parse_path(path)
        except FormatError as error:
            raise FormatError("%s: %s" % (path.name, error)) from error
        parsed["path"] = path.as_posix()
        files.append(parsed)

    families: Dict[str, int] = {}
    for parsed in files:
        family = str(parsed["family"])
        families[family] = families.get(family, 0) + 1
    return {
        "format": "nog2-vol-inventory-v1",
        "generated_by": "tools/nog2vol/nog2vol.py",
        "input_directory": input_directory.as_posix(),
        "file_count": len(files),
        "total_size": sum(int(parsed["size"]) for parsed in files),
        "family_counts": families,
        "files": files,
    }


def command_inventory(args: argparse.Namespace) -> int:
    inventory = build_inventory(Path(args.input_directory))
    content = serialized(inventory)
    output = Path(args.output)
    if args.check:
        if not output.is_file():
            print("missing %s" % output, file=sys.stderr)
            return 1
        if output.read_bytes() != content:
            print("stale %s" % output, file=sys.stderr)
            return 1
        print("ok %s: %d files, %d bytes" % (output, inventory["file_count"], inventory["total_size"]))
        return 0
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(content)
    print("wrote %s: %d files, %d bytes" % (output, inventory["file_count"], inventory["total_size"]))
    return 0


def command_verify(args: argparse.Namespace) -> int:
    parsed = parse_path(Path(args.path))
    print(json.dumps(parsed, indent=2, sort_keys=True))
    return 0


def command_extract(args: argparse.Namespace) -> int:
    result = extract_release(Path(args.input_directory), Path(args.output_directory))
    print(
        "extracted %d files, %d records, %d images, %d audio records to %s"
        % (
            result["file_count"],
            result["record_count"],
            result["image_count"],
            result["audio_count"],
            args.output_directory,
        )
    )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    inventory = subparsers.add_parser("inventory", help="parse the exact 31-file release VOL set")
    inventory.add_argument("input_directory")
    inventory.add_argument("output")
    inventory.add_argument("--check", action="store_true", help="verify that OUTPUT is current")
    inventory.set_defaults(function=command_inventory)

    verify = subparsers.add_parser("verify", help="strictly parse one source-known VOL file")
    verify.add_argument("path")
    verify.set_defaults(function=command_verify)

    extract = subparsers.add_parser("extract", help="losslessly extract records and normalized previews")
    extract.add_argument("input_directory")
    extract.add_argument("output_directory")
    extract.set_defaults(function=command_extract)
    return parser


def main(argv: Sequence[str]) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return int(args.function(args))
    except (FormatError, OSError, ValueError) as error:
        print("nog2vol: %s" % error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
