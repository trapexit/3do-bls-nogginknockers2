#!/usr/bin/env python3
"""Render the preserved AWE32 MIDI arrangement entirely offline."""

import argparse
import hashlib
import pathlib
import shutil
import subprocess
import sys


def file_sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while True:
            block = source.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def parse_args(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--midi", type=pathlib.Path, required=True)
    parser.add_argument("--timbres", type=pathlib.Path, required=True)
    parser.add_argument("--renderer", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--soundfont", type=pathlib.Path)
    parser.add_argument("--soundfont-sha256")
    return parser.parse_args(argv)


def render_fallback(args):
    subprocess.run(
        [
            str(args.renderer),
            str(args.midi),
            str(args.timbres),
            str(args.output),
        ],
        check=True,
    )


def render_soundfont(args):
    if args.soundfont_sha256 is None:
        raise ValueError(
            "--soundfont-sha256 is required with --soundfont"
        )
    expected = args.soundfont_sha256.lower()
    if len(expected) != 64 or any(
        character not in "0123456789abcdef" for character in expected
    ):
        raise ValueError("invalid --soundfont-sha256")
    actual = file_sha256(args.soundfont)
    if actual != expected:
        raise ValueError(
            "SoundFont SHA-256 mismatch: expected %s, got %s"
            % (expected, actual)
        )
    executable = shutil.which("fluidsynth")
    if executable is None:
        raise ValueError("fluidsynth is required for SoundFont rendering")
    subprocess.run(
        [
            executable,
            "-ni",
            "-a",
            "file",
            "-F",
            str(args.output),
            "-r",
            "44100",
            "-g",
            "1.0",
            str(args.soundfont),
            str(args.midi),
        ],
        check=True,
    )


def main(argv=None):
    args = parse_args(argv)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    try:
        if args.soundfont is None:
            render_fallback(args)
        else:
            render_soundfont(args)
    except (OSError, subprocess.CalledProcessError, ValueError) as error:
        print("AWE32 offline render failed: %s" % error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
