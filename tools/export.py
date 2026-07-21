#!/usr/bin/env python3
"""Export normalized NK2 game and music assets outside the build."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = ROOT / "orig" / "exe"
DEFAULT_GAME_OUTPUT = ROOT / "build" / "assets" / "normalized"
DEFAULT_MUSIC_OUTPUT = ROOT / "build" / "music" / "source"


def make_staging_directory(destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    return Path(
        tempfile.mkdtemp(
            prefix=".%s-export-" % destination.name,
            dir=destination.parent,
        )
    )


def replace_path_prefix(value, source, destination):
    if isinstance(value, dict):
        return {
            key: replace_path_prefix(item, source, destination)
            for key, item in value.items()
        }
    if isinstance(value, list):
        return [
            replace_path_prefix(item, source, destination)
            for item in value
        ]
    if not isinstance(value, str):
        return value
    source_prefix = str(source) + os.sep
    if value.startswith(source_prefix):
        return str(destination) + value[len(str(source)):]
    return value


def rewrite_manifest_paths(staging, destination):
    for manifest in staging.rglob("manifest.json"):
        content = json.loads(manifest.read_text(encoding="utf-8"))
        content = replace_path_prefix(content, staging, destination)
        manifest.write_text(
            json.dumps(content, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )


def replace_directories(replacements):
    backups = []
    try:
        for staging, destination in replacements:
            backup = destination.parent / (".%s.previous" % destination.name)
            if backup.exists():
                shutil.rmtree(backup)
            if destination.exists():
                destination.replace(backup)
            backups.append((destination, backup))
        for staging, destination in replacements:
            staging.replace(destination)
    except BaseException:
        for destination, backup in reversed(backups):
            if destination.exists():
                shutil.rmtree(destination)
            if backup.exists():
                backup.replace(destination)
        raise
    for _, backup in backups:
        if backup.exists():
            shutil.rmtree(backup)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument(
        "--game-output", type=Path, default=DEFAULT_GAME_OUTPUT
    )
    parser.add_argument(
        "--music-output", type=Path, default=DEFAULT_MUSIC_OUTPUT
    )
    args = parser.parse_args(argv)

    source = args.source.resolve()
    game_output = args.game_output.resolve()
    music_output = args.music_output.resolve()
    game_staging = None
    music_staging = None
    try:
        game_staging = make_staging_directory(game_output)
        music_staging = make_staging_directory(music_output)
        subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools" / "nog2vol" / "nog2vol.py"),
                "extract",
                str(source),
                str(game_staging),
            ],
            cwd=ROOT,
            check=True,
        )
        rewrite_manifest_paths(game_staging, game_output)
        subprocess.run(
            [
                sys.executable,
                str(ROOT / "tools" / "music" / "extract_music.py"),
                str(source),
                str(music_staging),
            ],
            cwd=ROOT,
            check=True,
        )
        replace_directories(
            (
                (game_staging, game_output),
                (music_staging, music_output),
            )
        )
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, "asset export failed: %s\n" % error)
    finally:
        for staging in (game_staging, music_staging):
            if (staging is not None) and staging.exists():
                shutil.rmtree(staging)
    print("exported normalized game and music assets")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
