#!/usr/bin/env python3
"""Build deterministic runtime CEL bundles from verified 3DO assets."""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import tempfile
from typing import Any, Dict, Iterable, List, Mapping, Sequence, Tuple


CHUNK_HEADER_SIZE = 8
CCB_CHUNK = b"CCB "
PDAT_CHUNK = b"PDAT"
PLUT_CHUNK = b"PLUT"
ALLOWED_CHUNKS = {CCB_CHUNK, PDAT_CHUNK, PLUT_CHUNK}
RUNTIME_FORMAT = "nog2-3do-runtime-assets-v3"
AUDIO_BANK_MAGIC = b"NKAB"
AUDIO_BANK_VERSION = 1
AUDIO_BANK_SAMPLE_RATE = 16000
AUDIO_BANK_HEADER_SIZE = 24
AUDIO_BANK_ENTRY_SIZE = 12

FIGHTER_DIRECTORIES = (
    "klubbor",
    "fetus",
    "henry",
    "gurdip",
    "ed",
    "sinammon",
    "buddy",
    "gonzoles",
)
CINEMA_DIRECTORIES = (
    "logo",
    "ncred",
    "end1",
    "end2",
    "end3",
    "end4",
    "end5",
    "end6",
    "end7",
    "end8",
)
PORT_CREDIT_CEL = "port/zombie_trap.cel"
HEAD_BANK_SOUND_IDS = (10, 12, 13, 14, 15, 16)
SELECT_SCREAM_MASKS = (0x03, 0x03, 0x03, 0x03, 0x03, 0x07, 0x07, 0x03)
FIGHTER_COMBAT_SOUND_IDS = (
    (5, 7, 8),
    (5, 6, 7, 8),
    (5, 6, 7, 8, 9),
    (5, 6, 7, 8, 9, 10),
    (5, 6, 7, 8, 9),
    (5, 8),
    (5, 6, 7, 8, 10, 11),
    (5, 6, 7),
)


class BundleError(Exception):
    """A runtime bundle input or output violated its contract."""


def ensure(condition: bool, message: str) -> None:
    if not condition:
        raise BundleError(message)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def serialized(value: Mapping[str, Any]) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")


def align_four(value: int) -> int:
    return (value + 3) & ~3


def aiff_pcm(path: Path) -> bytes:
    data = path.read_bytes()
    ensure(
        len(data) >= 12
        and data[:4] == b"FORM"
        and data[8:12] == b"AIFF",
        "%s: not an AIFF file" % path,
    )
    offset = 12
    frames = -1
    sound = None
    while offset < len(data):
        ensure(offset + 8 <= len(data), "%s: truncated AIFF chunk" % path)
        chunk_id, chunk_size = struct.unpack_from(">4sI", data, offset)
        chunk_start = offset + 8
        chunk_end = chunk_start + chunk_size
        ensure(chunk_end <= len(data), "%s: truncated AIFF payload" % path)
        if chunk_id == b"COMM":
            ensure(chunk_size == 18, "%s: unexpected COMM size" % path)
            channels, frames, width = struct.unpack_from(
                ">hIh", data, chunk_start
            )
            ensure(channels == 1, "%s: bank audio is not mono" % path)
            ensure(width == 8, "%s: bank audio is not 8-bit" % path)
            ensure(
                data[chunk_start + 8 : chunk_start + 18]
                == b"\x40\x0c\xfa\x00\x00\x00\x00\x00\x00\x00",
                "%s: bank audio is not 16 kHz" % path,
            )
        elif chunk_id == b"SSND":
            ensure(chunk_size >= 8, "%s: short SSND chunk" % path)
            sound_offset, block_size = struct.unpack_from(
                ">II", data, chunk_start
            )
            ensure(sound_offset == 0, "%s: nonzero SSND offset" % path)
            ensure(block_size == 0, "%s: nonzero SSND block size" % path)
            sound = data[chunk_start + 8 : chunk_end]
        offset = chunk_end + (chunk_size & 1)
    ensure(offset == len(data), "%s: AIFF framing mismatch" % path)
    ensure(frames >= 0, "%s: missing COMM chunk" % path)
    ensure(sound is not None, "%s: missing SSND chunk" % path)
    ensure(len(sound) == frames, "%s: frame count differs from PCM" % path)
    return sound


def audio_bank_bytes(
    asset_root: Path,
    entries: Sequence[Tuple[int, str]],
) -> Tuple[bytes, List[Dict[str, Any]], int]:
    ensure(entries, "audio bank has no members")
    ensure(
        len({logical_id for logical_id, _relative in entries})
        == len(entries),
        "audio bank has duplicate logical IDs",
    )
    data_offset = align_four(
        AUDIO_BANK_HEADER_SIZE + len(entries) * AUDIO_BANK_ENTRY_SIZE
    )
    output = bytearray(data_offset)
    members = []
    records = []
    maximum_size = 0
    for logical_id, relative in entries:
        path = asset_root / relative
        ensure(path.is_file(), "missing audio bank member: %s" % path)
        pcm = aiff_pcm(path)
        ensure(pcm, "empty audio bank member: %s" % path)
        member_offset = align_four(len(output))
        output.extend(b"\0" * (member_offset - len(output)))
        output.extend(pcm)
        records.append((logical_id, member_offset, len(pcm)))
        maximum_size = max(maximum_size, len(pcm))
        members.append(
            {
                "path": relative,
                "logical_id": logical_id,
                "offset": member_offset,
                "size": len(pcm),
                "sha256": sha256_bytes(pcm),
            }
        )
    file_size = len(output)
    struct.pack_into(
        ">4sIIIII",
        output,
        0,
        AUDIO_BANK_MAGIC,
        AUDIO_BANK_VERSION,
        AUDIO_BANK_SAMPLE_RATE,
        len(entries),
        data_offset,
        file_size,
    )
    for entry_index, record in enumerate(records):
        struct.pack_into(
            ">III",
            output,
            AUDIO_BANK_HEADER_SIZE + entry_index * AUDIO_BANK_ENTRY_SIZE,
            *record,
        )
    return bytes(output), members, maximum_size


def validate_cel(path: Path, data: bytes) -> None:
    """Validate the chunk framing required by Portfolio ParseCel."""
    offset = 0
    chunks: List[bytes] = []

    while offset < len(data):
        remaining = len(data) - offset
        ensure(remaining >= CHUNK_HEADER_SIZE, "%s: truncated chunk header" % path)
        chunk_id, chunk_size = struct.unpack_from(">4sI", data, offset)
        ensure(
            chunk_size >= CHUNK_HEADER_SIZE,
            "%s: invalid chunk size %d" % (path, chunk_size),
        )
        ensure(
            offset + chunk_size <= len(data),
            "%s: chunk extends beyond file" % path,
        )
        ensure(chunk_id in ALLOWED_CHUNKS, "%s: unknown CEL chunk" % path)
        chunks.append(chunk_id)
        offset += chunk_size

    ensure(offset == len(data), "%s: CEL framing does not cover file" % path)
    ensure(chunks.count(CCB_CHUNK) == 1, "%s: expected one CCB chunk" % path)
    ensure(chunks.count(PDAT_CHUNK) == 1, "%s: expected one PDAT chunk" % path)
    ensure(chunks.count(PLUT_CHUNK) <= 1, "%s: duplicate PLUT chunk" % path)
    ensure(chunks[0] == CCB_CHUNK, "%s: CCB is not the first chunk" % path)


def numeric_index(path: str) -> int:
    return int(Path(path).stem[1:4])


def relative_cel_path(asset_root: Path, image: Mapping[str, Any]) -> str:
    path = Path(str(image["cel"]["path"]))
    try:
        return path.relative_to(asset_root).as_posix()
    except ValueError as error:
        raise BundleError("CEL path is outside asset root: %s" % path) from error


def collect_images(
    manifest: Mapping[str, Any],
    asset_root: Path,
) -> Dict[str, Mapping[str, Any]]:
    images: Dict[str, Mapping[str, Any]] = {}
    for file_entry in manifest["files"]:
        for image in file_entry["images"]:
            relative = relative_cel_path(asset_root, image)
            ensure(relative not in images, "duplicate CEL path: %s" % relative)
            images[relative] = image
    return images


def paths_matching(
    images: Mapping[str, Mapping[str, Any]],
    prefix: str,
    nested: bool = False,
) -> List[str]:
    result = []
    for path in images:
        if not path.startswith(prefix):
            continue
        remainder = path[len(prefix):]
        if not nested and "/" in remainder:
            continue
        result.append(path)
    return sorted(result, key=numeric_index)


def add_numbered(
    entries: List[Tuple[str, int, str]],
    images: Mapping[str, Mapping[str, Any]],
    prefix: str,
    target: str,
) -> None:
    for path in paths_matching(images, prefix):
        entries.append((target, numeric_index(path), path))


def add_layer(
    entries: List[Tuple[str, int, str]],
    images: Mapping[str, Mapping[str, Any]],
    path: str,
    target: str,
) -> None:
    ensure(path in images, "missing CEL: %s" % path)
    entries.append((target, 0, path))


def build_specs(
    manifest: Mapping[str, Any],
    asset_root: Path,
) -> List[Dict[str, Any]]:
    images = collect_images(manifest, asset_root)
    specs: List[Dict[str, Any]] = []

    fonts: List[Tuple[str, int, str]] = []
    add_numbered(fonts, images, "font/images/i", "NK_ASSET_TARGET_FONT_BLUE")
    add_numbered(fonts, images, "font2/images/i", "NK_ASSET_TARGET_FONT_RED")
    add_numbered(
        fonts, images, "font3/images/white/i", "NK_ASSET_TARGET_FONT_WHITE"
    )
    add_numbered(
        fonts, images, "font3/images/icer/i", "NK_ASSET_TARGET_FONT_ICER"
    )
    add_numbered(
        fonts, images, "font3/images/stump/i", "NK_ASSET_TARGET_FONT_STUMP"
    )
    specs.append(
        {
            "id": "NK_ASSET_BUNDLE_FONTS",
            "name": "fonts",
            "path": "bundles/fonts.cel",
            "entries": fonts,
        }
    )

    main_game: List[Tuple[str, int, str]] = []
    for fighter_index, directory in enumerate(FIGHTER_DIRECTORIES):
        add_numbered(
            main_game,
            images,
            "%s/images/i" % directory,
            "NK_ASSET_TARGET_FIGHTER_%d" % fighter_index,
        )
    add_numbered(main_game, images, "head/images/i", "NK_ASSET_TARGET_FIGHTER_8")
    add_numbered(main_game, images, "head/images/p", "NK_ASSET_TARGET_PAIN")
    add_numbered(main_game, images, "misc/images/i", "NK_ASSET_TARGET_FIGHTER_9")
    composite_paths = sorted(
        (
            path
            for path in images
            if path.startswith("misc/images/c") and path.endswith(".cel")
        ),
        key=lambda path: (
            int(Path(path).stem[1:4]),
            0 if Path(path).stem.endswith("n") else 1,
        ),
    )
    for composite_index, path in enumerate(composite_paths):
        main_game.append(("NK_ASSET_TARGET_EFFECT", composite_index, path))

    add_numbered(
        main_game, images, "nogginbg/images/i", "NK_ASSET_TARGET_GAME_SCENE"
    )
    for name, target in (
        ("l1", "NK_ASSET_TARGET_GAME_LAYER1_COMPOSITE"),
        ("l2", "NK_ASSET_TARGET_GAME_LAYER2"),
        ("l3", "NK_ASSET_TARGET_GAME_LAYER3"),
    ):
        add_layer(main_game, images, "nogginbg/images/%s.cel" % name, target)
    specs.append(
        {
            "id": "NK_ASSET_BUNDLE_MAIN_GAME",
            "name": "main_game",
            "path": "bundles/main_game.cel",
            "entries": main_game,
        }
    )

    select: List[Tuple[str, int, str]] = []
    add_numbered(
        select, images, "select/images/i", "NK_ASSET_TARGET_SELECT_SCENE"
    )
    add_numbered(
        select,
        images,
        "select/images/gray/i",
        "NK_ASSET_TARGET_SELECTOR_GRAY",
    )
    for name, target in (
        ("l1a", "NK_ASSET_TARGET_SELECT_LAYER1A"),
        ("l1b", "NK_ASSET_TARGET_SELECT_LAYER1B"),
        ("l2", "NK_ASSET_TARGET_SELECT_LAYER2"),
        ("l3", "NK_ASSET_TARGET_SELECT_LAYER3"),
    ):
        add_layer(select, images, "select/images/%s.cel" % name, target)
    add_numbered(
        select,
        images,
        "select2/images/i",
        "NK_ASSET_TARGET_SELECTOR_MARKS",
    )
    add_numbered(
        select,
        images,
        "select3/images/i",
        "NK_ASSET_TARGET_SELECTOR_OPTIONS",
    )
    specs.append(
        {
            "id": "NK_ASSET_BUNDLE_SELECT",
            "name": "select",
            "path": "bundles/select.cel",
            "entries": select,
        }
    )

    title: List[Tuple[str, int, str]] = []
    add_numbered(title, images, "nogtitle/images/i", "NK_ASSET_TARGET_SCENE")
    for name, target in (
        ("l1a", "NK_ASSET_TARGET_SCENE_LAYER1A"),
        ("l1b", "NK_ASSET_TARGET_SCENE_LAYER1B"),
        ("l2", "NK_ASSET_TARGET_SCENE_LAYER2"),
        ("l3", "NK_ASSET_TARGET_SCENE_LAYER3"),
    ):
        add_layer(title, images, "nogtitle/images/%s.cel" % name, target)
    specs.append(
        {
            "id": "NK_ASSET_BUNDLE_TITLE",
            "name": "title",
            "path": "bundles/title.cel",
            "entries": title,
        }
    )

    bundle_ids = (
        "NK_ASSET_BUNDLE_LOGO",
        "NK_ASSET_BUNDLE_CREDITS",
        "NK_ASSET_BUNDLE_ENDING_1",
        "NK_ASSET_BUNDLE_ENDING_2",
        "NK_ASSET_BUNDLE_ENDING_3",
        "NK_ASSET_BUNDLE_ENDING_4",
        "NK_ASSET_BUNDLE_ENDING_5",
        "NK_ASSET_BUNDLE_ENDING_6",
        "NK_ASSET_BUNDLE_ENDING_7",
        "NK_ASSET_BUNDLE_ENDING_8",
    )
    bundle_names = (
        "logo",
        "credits",
        "ending1",
        "ending2",
        "ending3",
        "ending4",
        "ending5",
        "ending6",
        "ending7",
        "ending8",
    )
    for bundle_id, bundle_name, directory in zip(
        bundle_ids, bundle_names, CINEMA_DIRECTORIES
    ):
        entries: List[Tuple[str, int, str]] = []
        add_numbered(
            entries, images, "%s/images/i" % directory, "NK_ASSET_TARGET_CINEMA"
        )
        if bundle_id == "NK_ASSET_BUNDLE_CREDITS":
            entries.append(("NK_ASSET_TARGET_PORT_CREDIT", 0, PORT_CREDIT_CEL))
        specs.append(
            {
                "id": bundle_id,
                "name": bundle_name,
                "path": "bundles/%s.cel" % bundle_name,
                "entries": entries,
            }
        )

    used_paths = {
        path
        for spec in specs
        for _target, _index, path in spec["entries"]
    }
    ensure(
        len(used_paths)
        == sum(len(spec["entries"]) for spec in specs),
        "a CEL appears in more than one runtime bundle",
    )
    return specs


def source_manifest_path(manifest_path: Path, value: str) -> Path:
    path = Path(value)
    if path.is_absolute():
        return path
    if path.exists():
        return path
    candidate = manifest_path.parent / path
    return candidate


def render_c_data(specs: Sequence[Mapping[str, Any]]) -> bytes:
    lines = [
        "/* Generated by tools/nog2vol/bundle_3do.py. */",
        '#include "nk_assets.h"',
        "",
    ]
    for spec in specs:
        variable = "g_ASSET_%s_ENTRIES" % str(spec["name"]).upper()
        lines.append("static const NkAssetMapEntry %s[] =" % variable)
        lines.append("{")
        for target, index, _path in spec["entries"]:
            lines.append(
                "  NK_ASSET_MAP(%s, %dU)," % (target, index)
            )
        lines.extend(("};", ""))

    lines.append(
        "const NkAssetBundleSpec "
        "nk_asset_bundle_specs[NK_ASSET_BUNDLE_COUNT] ="
    )
    lines.append("{")
    for spec in specs:
        variable = "g_ASSET_%s_ENTRIES" % str(spec["name"]).upper()
        lines.append(
            '  { "nog2/%s", %s, %dU },'
            % (spec["path"], variable, len(spec["entries"]))
        )
    lines.extend(("};", ""))
    return ("\n".join(lines)).encode("ascii")


def bundle_bytes(
    asset_root: Path,
    entries: Sequence[Tuple[str, int, str]],
) -> Tuple[bytes, List[Dict[str, Any]]]:
    chunks = []
    members = []
    offset = 0
    for target, index, relative in entries:
        path = asset_root / relative
        ensure(path.is_file(), "missing bundle member: %s" % path)
        data = path.read_bytes()
        validate_cel(path, data)
        chunks.append(data)
        members.append(
            {
                "path": relative,
                "target": target,
                "index": index,
                "offset": offset,
                "size": len(data),
                "sha256": sha256_bytes(data),
            }
        )
        offset += len(data)
    return b"".join(chunks), members


def audio_bank_specs(
    manifest: Mapping[str, Any],
    manifest_path: Path,
    asset_root: Path,
) -> Tuple[List[Dict[str, Any]], set]:
    available = {}
    for file_entry in manifest["files"]:
        for audio in file_entry["audio"]:
            source = source_manifest_path(
                manifest_path, str(audio["aiff"]["path"])
            )
            try:
                relative = source.relative_to(asset_root).as_posix()
            except ValueError as error:
                raise BundleError(
                    "AIFF path is outside asset root: %s" % source
                ) from error
            ensure(relative not in available, "duplicate AIFF path: %s" % relative)
            available[relative] = audio

    fighter_specs = []
    fighter_entries = []
    for directory, sound_ids in zip(
        FIGHTER_DIRECTORIES, FIGHTER_COMBAT_SOUND_IDS
    ):
        entries = [
            (
                sound_id,
                "%s/audio/s%03d.aiff" % (directory, sound_id),
            )
            for sound_id in sound_ids
        ]
        fighter_entries.extend(entries)
        fighter_specs.append(
            {
                "name": "fighter_%s" % directory,
                "path": "audio/%s.nkab" % directory,
                "entries": entries,
            }
        )
    head_entries = [
        (sound_id, "head/audio/s%03d.aiff" % sound_id)
        for sound_id in HEAD_BANK_SOUND_IDS
    ]
    scream_entries = []
    for character, directory in enumerate(FIGHTER_DIRECTORIES):
        for scream_index in range(3):
            if SELECT_SCREAM_MASKS[character] & (1 << scream_index):
                scream_entries.append(
                    (
                        character * 3 + scream_index,
                        "%s/audio/s%03d.aiff"
                        % (directory, scream_index + 1),
                    )
                )
    for _logical_id, relative in (
        fighter_entries + head_entries + scream_entries
    ):
        ensure(relative in available, "missing banked AIFF: %s" % relative)

    omitted = {
        relative
        for relative in available
        if relative.startswith("head/audio/")
        or relative.split("/", 1)[0] in FIGHTER_DIRECTORIES
    }
    return (
        fighter_specs
        + [
            {
                "name": "head",
                "path": "audio/head.nkab",
                "entries": head_entries,
            },
            {
                "name": "select_screams",
                "path": "audio/select_screams.nkab",
                "entries": scream_entries,
            },
        ],
        omitted,
    )


def copy_audio(
    manifest: Mapping[str, Any],
    manifest_path: Path,
    asset_root: Path,
    output_root: Path,
    omitted: set,
) -> List[Dict[str, Any]]:
    entries = []
    for file_entry in manifest["files"]:
        for audio in file_entry["audio"]:
            source = source_manifest_path(
                manifest_path, str(audio["aiff"]["path"])
            )
            try:
                relative = source.relative_to(asset_root)
            except ValueError as error:
                raise BundleError(
                    "AIFF path is outside asset root: %s" % source
                ) from error
            if relative.as_posix() in omitted:
                continue
            destination = output_root / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
            entries.append(
                {
                    "path": relative.as_posix(),
                    "size": destination.stat().st_size,
                    "sha256": sha256_file(destination),
                }
            )
    return sorted(entries, key=lambda entry: str(entry["path"]))


def build_runtime(
    manifest_path: Path,
    output_root: Path,
    data_path: Path,
    check_data: bool,
) -> Mapping[str, Any]:
    manifest_bytes = manifest_path.read_bytes()
    manifest = json.loads(manifest_bytes)
    ensure(
        manifest.get("format") == "nog2-3do-assets-v4",
        "unsupported 3DO asset manifest",
    )
    asset_root = source_manifest_path(
        manifest_path, str(manifest["target_directory"])
    )
    ensure(asset_root.is_dir(), "3DO asset root does not exist")
    specs = build_specs(manifest, asset_root)
    bank_specs, omitted_audio = audio_bank_specs(
        manifest, manifest_path, asset_root
    )

    runtime_bundles = []
    runtime_audio_banks = []
    with tempfile.TemporaryDirectory(
        prefix="nog2-runtime-", dir=output_root.parent
    ) as temporary:
        staging = Path(temporary)
        for spec in specs:
            data, members = bundle_bytes(asset_root, spec["entries"])
            spec["byte_count"] = len(data)
            destination = staging / str(spec["path"])
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
            runtime_bundles.append(
                {
                    "name": spec["name"],
                    "path": spec["path"],
                    "cel_count": len(spec["entries"]),
                    "size": len(data),
                    "sha256": sha256_bytes(data),
                    "members": members,
                }
            )

        for bank_spec in bank_specs:
            data, members, maximum_size = audio_bank_bytes(
                asset_root, bank_spec["entries"]
            )
            destination = staging / str(bank_spec["path"])
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
            runtime_audio_banks.append(
                {
                    "name": bank_spec["name"],
                    "path": bank_spec["path"],
                    "sample_count": len(bank_spec["entries"]),
                    "maximum_sample_bytes": maximum_size,
                    "size": len(data),
                    "sha256": sha256_bytes(data),
                    "members": members,
                }
            )

        expected_data = render_c_data(specs)
        if check_data:
            ensure(data_path.is_file(), "generated C mapping is missing")
            ensure(
                data_path.read_bytes() == expected_data,
                "generated C mapping is stale; run the data command",
            )
        else:
            data_path.parent.mkdir(parents=True, exist_ok=True)
            data_path.write_bytes(expected_data)

        audio = copy_audio(
            manifest,
            manifest_path,
            asset_root,
            staging,
            omitted_audio,
        )
        runtime_manifest = {
            "format": RUNTIME_FORMAT,
            "generated_by": "tools/nog2vol/bundle_3do.py",
            "source_manifest_sha256": sha256_bytes(manifest_bytes),
            "bundles": runtime_bundles,
            "bundle_count": len(runtime_bundles),
            "bundled_cel_count": sum(
                int(bundle["cel_count"]) for bundle in runtime_bundles
            ),
            "source_cel_count": int(manifest["image_count"]),
            "omitted_runtime_cel_count": int(manifest["image_count"])
            - sum(int(bundle["cel_count"]) for bundle in runtime_bundles),
            "audio": audio,
            "audio_count": len(audio),
            "audio_banks": runtime_audio_banks,
            "audio_bank_count": len(runtime_audio_banks),
        }
        (staging / "manifest.json").write_bytes(serialized(runtime_manifest))
        if output_root.exists():
            shutil.rmtree(output_root)
        staging.rename(output_root)
    return runtime_manifest


def verify_runtime(
    source_manifest_path_value: Path,
    runtime_manifest_path: Path,
    data_path: Path,
) -> Mapping[str, Any]:
    with tempfile.TemporaryDirectory(prefix="nog2-runtime-verify-") as temporary:
        expected_root = Path(temporary) / "runtime"
        expected = build_runtime(
            source_manifest_path_value,
            expected_root,
            data_path,
            True,
        )
        actual = json.loads(runtime_manifest_path.read_text(encoding="utf-8"))
        ensure(actual == expected, "runtime asset manifest differs")
        actual_root = runtime_manifest_path.parent
        expected_files = {
            path.relative_to(expected_root).as_posix()
            for path in expected_root.rglob("*")
            if path.is_file()
        }
        actual_files = {
            path.relative_to(actual_root).as_posix()
            for path in actual_root.rglob("*")
            if path.is_file()
        }
        ensure(actual_files == expected_files, "runtime asset file set differs")
        for relative in expected_files:
            ensure(
                (actual_root / relative).read_bytes()
                == (expected_root / relative).read_bytes(),
                "runtime asset differs: %s" % relative,
            )
        return actual


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)

    data = subparsers.add_parser("data")
    data.add_argument("manifest", type=Path)
    data.add_argument("output", type=Path)
    data.add_argument("--check", action="store_true")

    build = subparsers.add_parser("build")
    build.add_argument("manifest", type=Path)
    build.add_argument("output", type=Path)
    build.add_argument("--data", required=True, type=Path)

    verify = subparsers.add_parser("verify")
    verify.add_argument("source_manifest", type=Path)
    verify.add_argument("runtime_manifest", type=Path)
    verify.add_argument("--data", required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.command == "data":
            manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
            asset_root = source_manifest_path(
                args.manifest, str(manifest["target_directory"])
            )
            specs = build_specs(manifest, asset_root)
            for spec in specs:
                data, _members = bundle_bytes(asset_root, spec["entries"])
                spec["byte_count"] = len(data)
            expected = render_c_data(specs)
            if args.check:
                ensure(args.output.is_file(), "generated C mapping is missing")
                ensure(
                    args.output.read_bytes() == expected,
                    "generated C mapping is stale",
                )
            else:
                args.output.parent.mkdir(parents=True, exist_ok=True)
                args.output.write_bytes(expected)
        elif args.command == "build":
            result = build_runtime(
                args.manifest, args.output, args.data, True
            )
            print(
                "bundled %d CELs into %d files; copied %d AIFFs; "
                "built %d audio banks"
                % (
                    result["bundled_cel_count"],
                    result["bundle_count"],
                    result["audio_count"],
                    result["audio_bank_count"],
                )
            )
        else:
            result = verify_runtime(
                args.source_manifest, args.runtime_manifest, args.data
            )
            print(
                "verified %d CELs in %d bundles, %d AIFFs, and "
                "%d audio banks"
                % (
                    result["bundled_cel_count"],
                    result["bundle_count"],
                    result["audio_count"],
                    result["audio_bank_count"],
                )
            )
    except (BundleError, KeyError, OSError, ValueError, json.JSONDecodeError) as error:
        print("bundle_3do.py: %s" % error)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
