#!/usr/bin/env python3
"""Convert a checked NK2 export and replace takeme/nog2 atomically."""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_GAME_INPUT = ROOT / "build" / "assets" / "normalized"
DEFAULT_MUSIC_INPUT = ROOT / "build" / "music" / "source"
DEFAULT_OUTPUT = ROOT / "takeme" / "nog2"
BANNER_SOURCE = ROOT / "assets" / "port" / "BannerScreen.png"
BANNER_OUTPUT = ROOT / "takeme" / "BannerScreen"


def run(command, capture=False):
    return subprocess.run(
        [str(value) for value in command],
        cwd=ROOT,
        check=True,
        capture_output=capture,
        text=capture,
    )


def build_music_renderer(cc, cxx):
    output = ROOT / "build" / "tools" / "render_music"
    output.parent.mkdir(parents=True, exist_ok=True)
    objects = (
        output.parent / "render_music.o",
        output.parent / "music_parser.o",
        output.parent / "music_util.o",
    )
    run(
        [
            cc, "-std=c89", "-pedantic", "-Wall", "-Wextra", "-Werror",
            "-O2", "-Itools/music", "-c",
            "tools/music/render_music.c", "-o", objects[0],
        ]
    )
    run(
        [
            cc, "-std=c89", "-pedantic", "-Wall", "-Wextra", "-Werror",
            "-O2", "-Itools/music", "-c",
            "tools/music/music.c", "-o", objects[1],
        ]
    )
    run(
        [
            cc, "-std=c89", "-pedantic", "-Wall", "-Wextra", "-Werror",
            "-O2", "-Itools/music", "-c",
            "tools/music/util.c", "-o", objects[2],
        ]
    )
    run(
        [
            cxx, "-std=c++14", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-parameter", "-O2", "-Itools/music",
            "-Itools/music/third_party/ymfm",
            "tools/music/ym3812_render.cpp",
            "tools/music/third_party/ymfm/ymfm_opl.cpp",
            "tools/music/third_party/ymfm/ymfm_adpcm.cpp",
            "tools/music/third_party/ymfm/ymfm_pcm.cpp",
            objects[0], objects[1], objects[2], "-o", output,
        ]
    )
    return output


def convert_aiff(source, destination, ffmpeg, ffprobe):
    temporary = destination.with_suffix(destination.suffix + ".tmp")
    run(
        [
            ffmpeg, "-nostdin", "-v", "error", "-y", "-i", source,
            "-ar", "44100", "-ac", "1", "-c:a", "pcm_s16be", "-f",
            "aiff", temporary,
        ]
    )
    probe = run(
        [
            ffprobe, "-v", "error", "-select_streams", "a:0",
            "-show_entries", "stream=duration_ts", "-of", "csv=p=0",
            temporary,
        ],
        capture=True,
    )
    frames = int(probe.stdout.strip())
    if frames % 2 == 0:
        temporary.replace(destination)
        return
    run(
        [
            ffmpeg, "-nostdin", "-v", "error", "-y", "-i", temporary,
            "-af", "apad=pad_len=1", "-c:a", "pcm_s16be", "-f", "aiff",
            destination,
        ]
    )
    temporary.unlink()


def render_music(args, music_input):
    renderer = build_music_renderer(args.cc, args.cxx)
    output = ROOT / "build" / "music" / "runtime"
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)
    run(
        [
            renderer, music_input / "title-midi.bin",
            music_input / "timbres.vol", output / "title-midi.wav",
        ]
    )
    run(
        [
            renderer, music_input / "match-cmf.bin",
            music_input / "timbres.vol", output / "match-cmf.wav",
        ]
    )
    awe32_command = [
        sys.executable,
        ROOT / "tools" / "music" / "render_awe32.py",
        "--midi", music_input / "title-awe32.bin",
        "--timbres", music_input / "timbres.vol",
        "--renderer", renderer,
        "--output", output / "title-awe32.wav",
    ]
    if args.soundfont is not None:
        if args.soundfont_sha256 is None:
            raise ValueError("--soundfont-sha256 is required with --soundfont")
        awe32_command.extend(
            [
                "--soundfont", args.soundfont,
                "--soundfont-sha256", args.soundfont_sha256,
            ]
        )
    run(awe32_command)
    for name in ("title-midi", "title-awe32", "match-cmf"):
        convert_aiff(
            output / (name + ".wav"),
            output / (name + ".aiff"),
            args.ffmpeg,
            args.ffprobe,
        )
    return output


def replace_directory(staging, destination):
    backup = destination.parent / (".%s.previous" % destination.name)
    if backup.exists():
        shutil.rmtree(backup)
    if destination.exists():
        destination.replace(backup)
    try:
        staging.replace(destination)
    except BaseException:
        if backup.exists():
            backup.replace(destination)
        raise
    if backup.exists():
        shutil.rmtree(backup)


def convert_banner(three_it):
    temporary = BANNER_OUTPUT.with_name(".BannerScreen.tmp")
    if temporary.exists():
        temporary.unlink()
    try:
        run(
            [
                three_it, "to-banner", "-o", temporary, BANNER_SOURCE,
            ]
        )
        temporary.replace(BANNER_OUTPUT)
    except BaseException:
        if temporary.exists():
            temporary.unlink()
        raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-input", type=Path, default=DEFAULT_GAME_INPUT)
    parser.add_argument("--music-input", type=Path, default=DEFAULT_MUSIC_INPUT)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--three-it", default="3it")
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--cxx", default="c++")
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--ffprobe", default="ffprobe")
    parser.add_argument("--soundfont", type=Path)
    parser.add_argument("--soundfont-sha256")
    args = parser.parse_args(argv)

    game_input = args.game_input.resolve()
    music_input = args.music_input.resolve()
    output = args.output.resolve()
    target_assets = ROOT / "build" / "assets" / "3do"
    converter = ROOT / "tools" / "nog2vol" / "convert_3do.py"
    try:
        run(
            [
                sys.executable, converter, "convert-export", game_input,
                target_assets,
                "--3it", args.three_it,
            ]
        )
        port_directory = target_assets / "port"
        port_directory.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(
            ROOT / "assets" / "port" / "zombie_trap.cel",
            port_directory / "zombie_trap.cel",
        )
        manifest = target_assets / "manifest.json"
        run(
            [
                sys.executable, converter, "verify", manifest,
                "--3it", args.three_it,
            ]
        )
        bundle_tool = ROOT / "tools" / "nog2vol" / "bundle_3do.py"
        bundle_data = ROOT / "src" / "nk_asset_bundle_data.c"
        staged_bundle_data = (
            ROOT / "build" / "assets" / "nk_asset_bundle_data.c"
        )
        run(
            [
                sys.executable, bundle_tool, "data", manifest,
                staged_bundle_data,
            ]
        )
        output.parent.mkdir(parents=True, exist_ok=True)
        staging = Path(
            tempfile.mkdtemp(prefix=".%s-convert-" % output.name,
                             dir=output.parent)
        )
        try:
            run(
                [
                    sys.executable, bundle_tool, "build", manifest, staging,
                    "--data", staged_bundle_data,
                ]
            )
            run(
                [
                    sys.executable, bundle_tool, "verify", manifest,
                    staging / "manifest.json", "--data", staged_bundle_data,
                ]
            )
            shutil.copyfile(
                staging / "manifest.json",
                ROOT / "build" / "assets" / "runtime-manifest.json",
            )
            (staging / "manifest.json").unlink()
            music_output = render_music(args, music_input)
            (staging / "music").mkdir(parents=True)
            for name in ("title-midi", "title-awe32", "match-cmf"):
                shutil.copyfile(
                    music_output / (name + ".aiff"),
                    staging / "music" / (name + ".aiff"),
                )
            convert_banner(args.three_it)
            replace_directory(staging, output)
            bundle_data_temporary = bundle_data.with_suffix(".c.tmp")
            shutil.copyfile(staged_bundle_data, bundle_data_temporary)
            bundle_data_temporary.replace(bundle_data)
        except BaseException:
            if staging.exists():
                shutil.rmtree(staging)
            raise
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, "asset conversion failed: %s\n" % error)
    print("converted and staged release assets and banner in %s" % output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
