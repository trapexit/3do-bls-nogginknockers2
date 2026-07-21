#!/usr/bin/env python3
"""Convert normalized Noggin Knockers 2 assets to reviewed 3DO files."""

from __future__ import annotations

import argparse
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path
from typing import Any, Dict, List, Mapping, Sequence, Tuple

from assets import extract_release, safe_name
from formats import FormatError, ensure, parse_path, sha256_bytes


TRANSPARENT_RGBA = "0x01020300"
TARGET_AUDIO_RATE = 16000
CODED_COLOR_CAPACITIES = ((1, 2), (2, 4), (4, 16), (6, 32))
CEL_SELECTION_POLICY = "smallest-exact-packed"
CEL_RUNTIME_CONTRACT = "selected-cel-header-v1"
EFFECT_COMPOSITE_FRAMES = (61, 62, 63, 64, 65, 66)
SCENE_LAYER1_WIDTH = 640
SCENE_LAYER_HEIGHT = 100
CCB_PACKED = 0x00000200
CCB_BGND = 0x00000020
CCB_CCBPRE = 0x00400000
CCB_LDPLUT = 0x00800000
PIXC_OPAQUE = 0x1F001F00
PRE0_UNCODED = 0x00000010
PRE0_BPP_MASK = 0x00000007
PRE0_BPP_VALUES = {1: 1, 2: 2, 3: 4, 4: 6, 5: 8, 6: 16}


def serialized(value: Any) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")


def read_png_rgba(path: Path) -> Tuple[int, int, bytes]:
    data = path.read_bytes()
    ensure(data.startswith(b"\x89PNG\r\n\x1a\n"), "%s is not a PNG" % path)
    position = 8
    width = height = 0
    compressed = bytearray()
    while position < len(data):
        ensure(position + 12 <= len(data), "%s has a truncated PNG chunk" % path)
        size = struct.unpack_from(">I", data, position)[0]
        kind = data[position + 4 : position + 8]
        start = position + 8
        end = start + size
        ensure(end + 4 <= len(data), "%s has a truncated PNG payload" % path)
        if kind == b"IHDR":
            width, height, depth, color, compression, filtering, interlace = struct.unpack(
                ">IIBBBBB", data[start:end]
            )
            ensure(depth == 8 and color == 6, "%s is not RGBA8888" % path)
            ensure(compression == filtering == interlace == 0, "%s uses unsupported PNG options" % path)
        elif kind == b"IDAT":
            compressed.extend(data[start:end])
        elif kind == b"IEND":
            break
        position = end + 4
    ensure(width > 0 and height > 0, "%s has no PNG dimensions" % path)
    raw = zlib.decompress(bytes(compressed))
    stride = width * 4
    ensure(len(raw) == height * (stride + 1), "%s has an invalid PNG data size" % path)
    output = bytearray(width * height * 4)
    previous = bytearray(stride)
    source = 0
    for row in range(height):
        filter_type = raw[source]
        source += 1
        current = bytearray(raw[source : source + stride])
        source += stride
        ensure(filter_type <= 4, "%s uses an invalid PNG filter" % path)
        for index in range(stride):
            left = current[index - 4] if index >= 4 else 0
            up = previous[index]
            upper_left = previous[index - 4] if index >= 4 else 0
            if filter_type == 1:
                current[index] = (current[index] + left) & 0xFF
            elif filter_type == 2:
                current[index] = (current[index] + up) & 0xFF
            elif filter_type == 3:
                current[index] = (current[index] + ((left + up) // 2)) & 0xFF
            elif filter_type == 4:
                estimate = left + up - upper_left
                left_distance = abs(estimate - left)
                up_distance = abs(estimate - up)
                upper_left_distance = abs(estimate - upper_left)
                if left_distance <= up_distance and left_distance <= upper_left_distance:
                    predictor = left
                elif up_distance <= upper_left_distance:
                    predictor = up
                else:
                    predictor = upper_left
                current[index] = (current[index] + predictor) & 0xFF
        output[row * stride : (row + 1) * stride] = current
        previous = current
    return width, height, bytes(output)


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    body = kind + payload
    return (
        struct.pack(">I", len(payload))
        + body
        + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
    )


def write_png_rgba(path: Path, width: int, height: int, pixels: bytes) -> None:
    ensure(width > 0 and height > 0, "cannot write an empty PNG")
    ensure(
        len(pixels) == width * height * 4,
        "RGBA pixel count does not match PNG dimensions",
    )
    rows = bytearray()
    stride = width * 4
    for row in range(height):
        rows.append(0)
        rows.extend(pixels[row * stride : (row + 1) * stride])
    content = (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(
            b"IHDR",
            struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0),
        )
        + png_chunk(b"IDAT", zlib.compress(bytes(rows), 9))
        + png_chunk(b"IEND", b"")
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content)


def build_effect_composite_images(
    normalized_root: Path,
    manifest: Mapping[str, Any],
) -> List[Dict[str, Any]]:
    records = {
        str(entry["role"]): entry for entry in manifest["records"]
    }
    frames = records["effects.frames"]["metadata"]["frames"]
    source_images = {
        int(image["source"]["index"]): image for image in manifest["images"]
    }
    output: List[Dict[str, Any]] = []

    for frame_index in EFFECT_COMPOSITE_FRAMES:
        frame = frames[frame_index]
        ensure(
            len(frame["images"]) == 6,
            "effect composite frame %d is no longer six images" % frame_index,
        )
        for direction_name, flipped in (("normal", False), ("flipped", True)):
            layers: List[Dict[str, Any]] = []
            for reference in frame["images"]:
                image_index = int(reference["image"])
                ensure(
                    image_index in source_images,
                    "effect composite references missing image %d" % image_index,
                )
                source = source_images[image_index]
                width, height, pixels = read_png_rgba(
                    Path(str(source["normalized_png"]["path"]))
                )
                ensure(
                    int(reference["orientation"]) == 0,
                    "effect composite frame %d has a pre-oriented layer"
                    % frame_index,
                )
                layers.append(
                    {
                        "image": image_index,
                        "x": int(
                            reference[
                                "x_flipped" if flipped else "x_normal"
                            ]
                        ),
                        "y": int(reference["y"]),
                        "width": width,
                        "height": height,
                        "pixels": pixels,
                        "source_sha256": source["normalized_png"]["sha256"],
                    }
                )
            minimum_x = min(int(layer["x"]) for layer in layers)
            minimum_y = min(int(layer["y"]) for layer in layers)
            maximum_x = max(
                int(layer["x"]) + int(layer["width"]) for layer in layers
            )
            maximum_y = max(
                int(layer["y"]) + int(layer["height"]) for layer in layers
            )
            composite_width = maximum_x - minimum_x
            composite_height = maximum_y - minimum_y
            composite = bytearray(b"\x01\x02\x03\x00") * (
                composite_width * composite_height
            )
            for layer in layers:
                layer_width = int(layer["width"])
                layer_height = int(layer["height"])
                layer_pixels = bytes(layer["pixels"])
                for source_y in range(layer_height):
                    for source_x in range(layer_width):
                        source_offset = (
                            source_y * layer_width + source_x
                        ) * 4
                        if layer_pixels[source_offset + 3] == 0:
                            continue
                        draw_x = (
                            layer_width - 1 - source_x
                            if flipped
                            else source_x
                        )
                        target_x = int(layer["x"]) - minimum_x + draw_x
                        target_y = int(layer["y"]) - minimum_y + source_y
                        target_offset = (
                            target_y * composite_width + target_x
                        ) * 4
                        composite[target_offset : target_offset + 4] = (
                            layer_pixels[source_offset : source_offset + 4]
                        )
            name = "effects_composite_frame_%03d_%s" % (
                frame_index,
                direction_name,
            )
            path = (
                normalized_root
                / "derived"
                / "MISC"
                / (name + ".rgba.png")
            )
            write_png_rgba(
                path,
                composite_width,
                composite_height,
                bytes(composite),
            )
            content = path.read_bytes()
            output.append(
                {
                    "name": name,
                    "kind": "derived_effect_composite",
                    "effect_composite_frame": frame_index,
                    "effect_composite_direction": direction_name,
                    "effect_composite_origin_x": minimum_x,
                    "effect_composite_origin_y": minimum_y,
                    "source": {
                        "role": "effects.frames",
                        "frame": frame_index,
                        "direction": direction_name,
                        "layers": [
                            {
                                "image": int(layer["image"]),
                                "x": int(layer["x"]),
                                "y": int(layer["y"]),
                                "sha256": str(layer["source_sha256"]),
                            }
                            for layer in layers
                        ],
                    },
                    "normalized_png": {
                        "path": path.as_posix(),
                        "size": len(content),
                        "sha256": sha256_bytes(content),
                    },
                    "transparent": True,
                }
            )
    return output


def build_scene_layer1_composite_image(
    normalized_root: Path,
    manifest: Mapping[str, Any],
) -> Dict[str, Any]:
    sources = {
        str(image["name"]): image for image in manifest["images"]
    }
    source_names = ("scene_layer1a", "scene_layer1b")
    ensure(
        all(name in sources for name in source_names),
        "scene layer1 composite sources are incomplete",
    )
    layers: List[Dict[str, Any]] = []
    for name in source_names:
        source = sources[name]
        width, height, pixels = read_png_rgba(
            Path(str(source["normalized_png"]["path"]))
        )
        layers.append(
            {
                "name": name,
                "width": width,
                "height": height,
                "pixels": pixels,
                "sha256": source["normalized_png"]["sha256"],
            }
        )
    ensure(
        int(layers[0]["width"]) == SCENE_LAYER1_WIDTH
        and int(layers[1]["width"]) == SCENE_LAYER1_WIDTH,
        "scene layer1 composite width changed",
    )
    ensure(
        int(layers[0]["height"]) == SCENE_LAYER_HEIGHT
        and int(layers[1]["height"]) == SCENE_LAYER_HEIGHT,
        "scene layer1 composite layout changed",
    )
    width = int(layers[0]["width"])
    height = int(layers[0]["height"]) + int(layers[1]["height"])
    pixels = bytes(layers[0]["pixels"]) + bytes(layers[1]["pixels"])
    path = (
        normalized_root
        / "derived"
        / "NOGGINBG"
        / "scene_layer1.rgba.png"
    )
    write_png_rgba(path, width, height, pixels)
    content = path.read_bytes()
    return {
        "name": "scene_layer1",
        "kind": "derived_scene_layer_composite",
        "source": {
            "role": "scene.layer1_composite",
            "layers": [
                {
                    "name": str(layer["name"]),
                    "y": index * SCENE_LAYER_HEIGHT,
                    "sha256": str(layer["sha256"]),
                }
                for index, layer in enumerate(layers)
            ],
        },
        "normalized_png": {
            "path": path.as_posix(),
            "size": len(content),
            "sha256": sha256_bytes(content),
        },
        "transparent": True,
    }


def compare_cel_roundtrip(source: Path, decoded: Path) -> Dict[str, int]:
    source_width, source_height, source_pixels = read_png_rgba(source)
    target_width, target_height, target_pixels = read_png_rgba(decoded)
    ensure(
        (source_width, source_height) == (target_width, target_height),
        "CEL round trip changed dimensions for %s" % source,
    )
    visible = 0
    transparent = 0
    alpha_errors = 0
    maximum_error = 0
    total_error = 0
    for offset in range(0, len(source_pixels), 4):
        source_alpha = source_pixels[offset + 3]
        target_alpha = target_pixels[offset + 3]
        if (source_alpha == 0):
            transparent += 1
            if target_alpha != 0:
                alpha_errors += 1
            continue
        visible += 1
        if target_alpha == 0:
            alpha_errors += 1
            continue
        for component in range(3):
            error = abs(source_pixels[offset + component] - target_pixels[offset + component])
            maximum_error = max(maximum_error, error)
            total_error += error
    ensure(alpha_errors == 0, "CEL round trip changed the alpha mask for %s" % source)
    ensure(maximum_error <= 8, "CEL round trip exceeded RGB error 8 for %s" % source)
    return {
        "width": source_width,
        "height": source_height,
        "visible_pixels": visible,
        "transparent_pixels": transparent,
        "alpha_errors": alpha_errors,
        "maximum_component_error": maximum_error,
        "total_component_error": total_error,
    }


def visible_color_count(path: Path) -> int:
    _width, _height, pixels = read_png_rgba(path)
    return len(
        {
            pixels[offset : offset + 4]
            for offset in range(0, len(pixels), 4)
            if pixels[offset + 3] != 0
        }
    )


def decoded_visible_color_count(path: Path) -> int:
    _width, _height, pixels = read_png_rgba(path)
    return len(
        {
            pixels[offset : offset + 3]
            for offset in range(0, len(pixels), 4)
            if pixels[offset + 3] != 0
        }
    )


def decoded_cels_equivalent(reference: Path, candidate: Path) -> bool:
    reference_width, reference_height, reference_pixels = read_png_rgba(reference)
    candidate_width, candidate_height, candidate_pixels = read_png_rgba(candidate)
    if (reference_width, reference_height) != (candidate_width, candidate_height):
        return False
    for offset in range(0, len(reference_pixels), 4):
        reference_alpha = reference_pixels[offset + 3]
        candidate_alpha = candidate_pixels[offset + 3]
        if reference_alpha != candidate_alpha:
            return False
        if reference_alpha != 0 and (
            reference_pixels[offset : offset + 3]
            != candidate_pixels[offset : offset + 3]
        ):
            return False
    return True


def decoded_semantic_sha256(path: Path) -> str:
    width, height, pixels = read_png_rgba(path)
    canonical = bytearray(struct.pack(">II", width, height))
    for offset in range(0, len(pixels), 4):
        if pixels[offset + 3] == 0:
            canonical.extend(b"\0\0\0\0")
        else:
            canonical.extend(pixels[offset : offset + 4])
    return sha256_bytes(bytes(canonical))


def coded_bpps_for_visible_colors(visible_colors: int) -> List[int]:
    ensure(visible_colors >= 0, "visible color count is negative")
    required_colors = max(1, visible_colors)
    return [
        bpp
        for bpp, capacity in CODED_COLOR_CAPACITIES
        if required_colors <= capacity
    ]


def select_smallest_exact_cel(
    baseline_size: int,
    coded_candidates: Sequence[Mapping[str, Any]],
) -> Dict[str, Any]:
    ensure(baseline_size > 0, "uncoded CEL baseline is empty")
    selected_coded = False
    selected_bpp = 16
    selected_size = baseline_size
    for candidate in coded_candidates:
        ensure(
            candidate["exact_to_uncoded_16"] is True,
            "coded CEL candidate is not exact",
        )
        candidate_size = int(candidate["size"])
        if candidate_size < selected_size:
            selected_coded = True
            selected_bpp = int(candidate["bpp"])
            selected_size = candidate_size
    return {
        "coded": selected_coded,
        "bpp": selected_bpp,
        "size": selected_size,
    }


def read_cel_encoding(path: Path) -> Dict[str, Any]:
    content = path.read_bytes()
    ensure(len(content) >= 80, "CEL has no complete CCB: %s" % path)
    ensure(content[:4] == b"CCB ", "CEL does not begin with a CCB: %s" % path)
    flags = struct.unpack_from(">I", content, 12)[0]
    pixc = struct.unpack_from(">I", content, 60)[0]
    pre0 = struct.unpack_from(">I", content, 64)[0]
    bpp_code = pre0 & PRE0_BPP_MASK
    ensure(bpp_code in PRE0_BPP_VALUES, "CEL has invalid bpp: %s" % path)
    return {
        "coded": not bool(pre0 & PRE0_UNCODED),
        "bpp": PRE0_BPP_VALUES[bpp_code],
        "packed": bool(flags & CCB_PACKED),
        "ccb_bgnd": bool(flags & CCB_BGND),
        "ccb_ccbpre": bool(flags & CCB_CCBPRE),
        "ccb_ldplut": bool(flags & CCB_LDPLUT),
        "pixc": "0x%08x" % pixc,
    }


def validate_cel_runtime_contract(path: Path, encoding: Mapping[str, Any]) -> None:
    ensure(
        encoding["pixc"] == "0x%08x" % PIXC_OPAQUE,
        "CEL PIXC is not PIXC_OPAQUE 0x%08x: %s" % (PIXC_OPAQUE, path),
    )
    ensure(
        encoding["coded"] is False or encoding["ccb_ldplut"] is True,
        "coded CEL is missing CCB_LDPLUT: %s" % path,
    )


def run_checked(command: Sequence[str]) -> str:
    completed = subprocess.run(
        list(command),
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if completed.returncode != 0:
        raise FormatError(
            "command failed (%d): %s\n%s"
            % (completed.returncode, " ".join(command), completed.stdout)
        )
    return completed.stdout


def image_tool_identity(tool: str) -> Dict[str, str]:
    executable = shutil.which(tool)
    ensure(executable is not None, "image tool is not executable: %s" % tool)
    resolved = Path(str(executable)).resolve()
    version_lines = [
        line.strip() for line in run_checked([tool, "version"]).splitlines() if line.strip()
    ]
    ensure(bool(version_lines), "image tool returned no version: %s" % tool)
    return {
        "version": version_lines[0],
        "sha256": sha256_bytes(resolved.read_bytes()),
    }


def load_manifest(path: Path) -> Mapping[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def emit_cel(
    source: Path,
    cel: Path,
    decoded: Path,
    tool: str,
    coded: bool,
    bpp: int,
    transparent: bool,
) -> None:
    command = [
        tool,
        "to-cel",
        str(source),
        "--coded",
        "true" if coded else "false",
        "--bpp",
        str(bpp),
        "--packed",
        "true",
        "--transparent",
        TRANSPARENT_RGBA,
        "--ccb-bgnd",
        "unset" if transparent else "set",
        "-o",
        str(cel),
    ]
    run_checked(command)
    run_checked([tool, "to-png", str(cel), "-o", str(decoded)])


def convert_image(
    image: Mapping[str, Any],
    destination: Path,
    roundtrip: Path,
    tool: str,
) -> Dict[str, Any]:
    source = Path(str(image["normalized_png"]["path"]))
    destination.parent.mkdir(parents=True, exist_ok=True)
    roundtrip.parent.mkdir(parents=True, exist_ok=True)
    transparent = bool(image["transparent"])
    with tempfile.TemporaryDirectory(
        prefix=".nog2-cel-", dir=str(destination.parent)
    ) as temporary:
        candidate_root = Path(temporary)
        baseline_cel = candidate_root / "uncoded-16.cel"
        baseline_png = candidate_root / "uncoded-16.png"
        emit_cel(
            source,
            baseline_cel,
            baseline_png,
            tool,
            coded=False,
            bpp=16,
            transparent=transparent,
        )
        baseline_size = baseline_cel.stat().st_size
        visible_source_colors = visible_color_count(source)
        visible_target_colors = decoded_visible_color_count(baseline_png)
        selected_cel = baseline_cel
        selected_png = baseline_png
        coded_candidates: List[Dict[str, Any]] = []
        candidate_outputs: Dict[int, Tuple[Path, Path]] = {}
        for coded_bpp in coded_bpps_for_visible_colors(visible_source_colors):
            candidate_cel = candidate_root / ("coded-%d.cel" % coded_bpp)
            candidate_png = candidate_root / ("coded-%d.png" % coded_bpp)
            emit_cel(
                source,
                candidate_cel,
                candidate_png,
                tool,
                coded=True,
                bpp=coded_bpp,
                transparent=transparent,
            )
            exact = decoded_cels_equivalent(baseline_png, candidate_png)
            ensure(
                exact,
                "coded %dbpp CEL differs from uncoded 16bpp baseline for %s"
                % (coded_bpp, source),
            )
            candidate_size = candidate_cel.stat().st_size
            coded_candidates.append(
                {
                    "bpp": coded_bpp,
                    "size": candidate_size,
                    "exact_to_uncoded_16": exact,
                }
            )
            candidate_outputs[coded_bpp] = (candidate_cel, candidate_png)
        selected = select_smallest_exact_cel(baseline_size, coded_candidates)
        selected_coded = bool(selected["coded"])
        selected_bpp = int(selected["bpp"])
        selected_size = int(selected["size"])
        if selected_coded:
            selected_cel, selected_png = candidate_outputs[selected_bpp]
        shutil.copyfile(selected_cel, destination)
        shutil.copyfile(selected_png, roundtrip)

    metrics = compare_cel_roundtrip(source, roundtrip)
    content = destination.read_bytes()
    encoding = read_cel_encoding(destination)
    ensure(
        encoding["coded"] == selected_coded
        and encoding["bpp"] == selected_bpp
        and encoding["packed"] is True
        and encoding["ccb_bgnd"] == (not transparent),
        "selected CEL header differs from requested encoding: %s" % destination,
    )
    validate_cel_runtime_contract(destination, encoding)
    result = {
        "name": image["name"],
        "kind": image["kind"],
        "source": image["source"],
        "source_normalized_png": str(source),
        "source_normalized_sha256": image["normalized_png"]["sha256"],
        "transparent": transparent,
        "cel": {
            "path": destination.as_posix(),
            "size": len(content),
            "sha256": sha256_bytes(content),
            "decoded_semantic_sha256": decoded_semantic_sha256(roundtrip),
            "coded": encoding["coded"],
            "bpp": encoding["bpp"],
            "packed": encoding["packed"],
            "ccb_bgnd": encoding["ccb_bgnd"],
            "ccb_ccbpre": encoding["ccb_ccbpre"],
            "ccb_ldplut": encoding["ccb_ldplut"],
            "pixc": encoding["pixc"],
        },
        "cel_selection": {
            "policy": CEL_SELECTION_POLICY,
            "visible_source_colors": visible_source_colors,
            "visible_target_colors": visible_target_colors,
            "baseline": {
                "coded": False,
                "bpp": 16,
                "size": baseline_size,
            },
            "coded_candidates": coded_candidates,
            "selected_coded": selected_coded,
            "selected_bpp": selected_bpp,
            "selected_size": selected_size,
            "saved_bytes": baseline_size - selected_size,
            "exact_to_uncoded_16": True,
        },
        "roundtrip": metrics,
    }
    if "effect_composite_frame" in image:
        result["effect_composite_frame"] = image[
            "effect_composite_frame"
        ]
        result["effect_composite_direction"] = image[
            "effect_composite_direction"
        ]
        result["effect_composite_origin_x"] = image[
            "effect_composite_origin_x"
        ]
        result["effect_composite_origin_y"] = image[
            "effect_composite_origin_y"
        ]
    return result


def convert_audio(
    audio: Mapping[str, Any],
    destination: Path,
) -> Dict[str, Any]:
    source = Path(str(audio["aiff"]["path"]))
    destination.parent.mkdir(parents=True, exist_ok=True)
    content = source.read_bytes()
    ensure(
        len(content) >= 12
        and content[:4] == b"FORM"
        and content[8:12] == b"AIFF",
        "%s is not an uncompressed AIFF file" % source,
    )
    ensure(
        int(audio["sample_rate_hz"]) == TARGET_AUDIO_RATE,
        "%s does not use the source 16 kHz rate" % source,
    )
    shutil.copyfile(source, destination)
    return {
        "role": audio["role"],
        "played_samples": audio["played_samples"],
        "source_sample_rate_hz": audio["sample_rate_hz"],
        "target_sample_rate_hz": TARGET_AUDIO_RATE,
        "source_aiff": source.as_posix(),
        "source_aiff_sha256": audio["aiff"]["sha256"],
        "aiff": {
            "path": destination.as_posix(),
            "size": len(content),
            "sha256": sha256_bytes(content),
            "sample_format": "signed 8-bit mono PCM",
            "byte_identical_to_source": True,
        },
    }


def target_image_filename(image: Mapping[str, Any], ordinal: int) -> str:
    name = str(image["name"])
    if "effect_composite_frame" in image:
        suffix = (
            "n"
            if image["effect_composite_direction"] == "normal"
            else "f"
        )
        return "c%03d%s.cel" % (
            int(image["effect_composite_frame"]),
            suffix,
        )
    if "font_glyph_ordinal" in image:
        glyph_ordinal = int(image["font_glyph_ordinal"])
        variant = str(image.get("font_variant", "base"))
        if variant != "base":
            return "%s/i%03d.cel" % (variant, glyph_ordinal)
        return "i%03d.cel" % glyph_ordinal
    if "scene_image_index" in image:
        return "%s/i%03d.cel" % (
            image["scene_variant"], int(image["scene_image_index"])
        )
    match = re.search(r"(?:^|_)pain_image_blob_(\d+)$", name)
    if match:
        return "p%03d.cel" % int(match.group(1))
    match = re.search(r"(?:^|_)image_blob_(\d+)$", name)
    if match:
        return "i%03d.cel" % int(match.group(1))
    layers = {
        "scene_layer1": "l1.cel",
        "scene_layer1a": "l1a.cel",
        "scene_layer1b": "l1b.cel",
        "scene_layer2": "l2.cel",
        "scene_layer3": "l3.cel",
    }
    return layers.get(name, "i%03d.cel" % ordinal)


def convert_extracted_release(
    extracted: Mapping[str, Any],
    normalized_root: Path,
    target_root: Path,
    image_tool: str,
) -> Dict[str, Any]:
    ensure(
        extracted.get("format") == "nog2-extracted-release-v1",
        "unsupported normalized asset manifest",
    )
    image_tool_info = image_tool_identity(image_tool)
    if target_root.exists():
        shutil.rmtree(target_root)
    files: List[Dict[str, Any]] = []
    image_total = 0
    audio_total = 0
    baseline_cel_bytes = 0
    selected_cel_bytes = 0
    coded_images = 0
    ccbpre_images = 0
    bpp_counts: Dict[str, int] = {}
    for summary in extracted["files"]:
        manifest = load_manifest(
            normalized_root / Path(str(summary["name"])).stem / "manifest.json"
        )
        stem = safe_name(Path(str(manifest["source"]["name"])).stem.lower())
        target_directory = target_root / stem
        roundtrip_directory = normalized_root / "roundtrip" / stem
        images: List[Dict[str, Any]] = []
        audio: List[Dict[str, Any]] = []
        release_images = list(manifest["images"])
        if manifest["source"]["name"] == "MISC.VOL":
            release_images.extend(
                build_effect_composite_images(
                    normalized_root,
                    manifest,
                )
            )
        elif manifest["source"]["name"] == "NOGGINBG.VOL":
            release_images.append(
                build_scene_layer1_composite_image(
                    normalized_root,
                    manifest,
                )
            )
        for image_ordinal, image in enumerate(release_images):
            name = safe_name(str(image["name"]))
            converted = convert_image(
                image,
                target_directory / "images"
                / target_image_filename(image, image_ordinal),
                roundtrip_directory / (name + ".png"),
                image_tool,
            )
            images.append(converted)
            baseline_cel_bytes += int(
                converted["cel_selection"]["baseline"]["size"]
            )
            selected_cel_bytes += int(converted["cel"]["size"])
            coded_images += int(bool(converted["cel"]["coded"]))
            ccbpre_images += int(bool(converted["cel"]["ccb_ccbpre"]))
            bpp_key = str(converted["cel"]["bpp"])
            bpp_counts[bpp_key] = bpp_counts.get(bpp_key, 0) + 1
        for sound_ordinal, sound in enumerate(manifest["audio"]):
            if int(sound["stored_samples"]) == 0:
                continue
            audio.append(
                convert_audio(
                    sound,
                    target_directory / "audio" / ("s%03d.aiff" % sound_ordinal),
                )
            )
        image_total += len(images)
        audio_total += len(audio)
        files.append(
            {
                "name": manifest["source"]["name"],
                "family": manifest["source"]["family"],
                "source_sha256": manifest["source"]["sha256"],
                "images": images,
                "audio": audio,
            }
        )
    result = {
        "format": "nog2-3do-assets-v4",
        "generated_by": "tools/nog2vol/convert_3do.py",
        "source_directory": str(extracted["source_directory"]),
        "normalized_directory": normalized_root.as_posix(),
        "target_directory": target_root.as_posix(),
        "image_tool": image_tool_info,
        "audio_codec": "uncompressed signed 8-bit PCM",
        "audio_rate_hz": TARGET_AUDIO_RATE,
        "transparent_rgba": TRANSPARENT_RGBA,
        "file_count": len(files),
        "image_count": image_total,
        "audio_count": audio_total,
        "cel_policy": {
            "name": CEL_SELECTION_POLICY,
            "packed": True,
            "coded_color_capacities": {
                str(bpp): capacity for bpp, capacity in CODED_COLOR_CAPACITIES
            },
            "uncoded_fallback_bpp": 16,
            "equivalence": "exact alpha and exact visible RGB versus packed uncoded 16bpp",
            "baseline_bytes": baseline_cel_bytes,
            "selected_bytes": selected_cel_bytes,
            "saved_bytes": baseline_cel_bytes - selected_cel_bytes,
            "coded_images": coded_images,
            "uncoded_images": image_total - coded_images,
            "bpp_counts": bpp_counts,
        },
        "runtime_contract": {
            "name": CEL_RUNTIME_CONTRACT,
            "selected_cel_pixc": "0x%08x" % PIXC_OPAQUE,
            "coded_cel_requires_ccb_ldplut": True,
            "ccb_ccbpre": {
                "required": False,
                "set_images": ccbpre_images,
                "unset_images": image_total - ccbpre_images,
            },
        },
        "files": files,
    }
    manifest_path = target_root / "manifest.json"
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_bytes(serialized(result))
    return result


def convert_release(
    vol_root: Path,
    normalized_root: Path,
    target_root: Path,
    image_tool: str,
) -> Dict[str, Any]:
    extracted = extract_release(vol_root, normalized_root)
    return convert_extracted_release(
        extracted,
        normalized_root,
        target_root,
        image_tool,
    )


def convert_export(
    normalized_root: Path,
    target_root: Path,
    image_tool: str,
) -> Dict[str, Any]:
    extracted = load_manifest(normalized_root / "manifest.json")
    return convert_extracted_release(
        extracted,
        normalized_root,
        target_root,
        image_tool,
    )


def verify_image_entry(
    image: Mapping[str, Any],
    decoded: Path,
    image_tool: str,
) -> Dict[str, int]:
    cel = image["cel"]
    path = Path(str(cel["path"]))
    content = path.read_bytes()
    ensure(len(content) == int(cel["size"]), "CEL size changed: %s" % path)
    ensure(sha256_bytes(content) == cel["sha256"], "CEL hash changed: %s" % path)
    source = Path(str(image["source_normalized_png"]))
    ensure(
        sha256_bytes(source.read_bytes()) == image["source_normalized_sha256"],
        "normalized source changed: %s" % source,
    )
    encoding = read_cel_encoding(path)
    for field in (
        "coded",
        "bpp",
        "packed",
        "ccb_bgnd",
        "ccb_ccbpre",
        "ccb_ldplut",
        "pixc",
    ):
        ensure(encoding[field] == cel[field], "CEL %s metadata changed: %s" % (field, path))
    validate_cel_runtime_contract(path, encoding)
    ensure(cel["packed"] is True, "CEL is not packed: %s" % path)

    selection = image["cel_selection"]
    ensure(
        selection["policy"] == CEL_SELECTION_POLICY,
        "unknown CEL selection policy: %s" % path,
    )
    ensure(
        selection["exact_to_uncoded_16"] is True,
        "CEL was not proven exact: %s" % path,
    )
    ensure(
        selection["baseline"]["coded"] is False
        and int(selection["baseline"]["bpp"]) == 16,
        "invalid CEL baseline metadata: %s" % path,
    )
    visible_source_colors = visible_color_count(source)
    ensure(
        int(selection["visible_source_colors"]) == visible_source_colors,
        "source color count changed: %s" % path,
    )
    expected_bpps = coded_bpps_for_visible_colors(visible_source_colors)
    candidates = selection["coded_candidates"]
    ensure(
        [int(candidate["bpp"]) for candidate in candidates] == expected_bpps,
        "coded CEL candidate depths changed: %s" % path,
    )
    baseline_size = int(selection["baseline"]["size"])
    for candidate in candidates:
        ensure(
            candidate["exact_to_uncoded_16"] is True,
            "coded CEL candidate was not exact: %s" % path,
        )
    expected = select_smallest_exact_cel(baseline_size, candidates)
    expected_coded = bool(expected["coded"])
    expected_bpp = int(expected["bpp"])
    expected_size = int(expected["size"])
    ensure(
        bool(cel["coded"]) == expected_coded
        and int(cel["bpp"]) == expected_bpp
        and len(content) == expected_size,
        "CEL is not the smallest exact packed candidate: %s" % path,
    )
    ensure(
        bool(selection["selected_coded"]) == expected_coded
        and int(selection["selected_bpp"]) == expected_bpp
        and int(selection["selected_size"]) == expected_size,
        "selected CEL metadata changed: %s" % path,
    )
    ensure(
        int(selection["saved_bytes"]) == baseline_size - expected_size,
        "CEL saving changed: %s" % path,
    )

    run_checked([image_tool, "to-png", str(path), "-o", str(decoded)])
    roundtrip = compare_cel_roundtrip(source, decoded)
    ensure(roundtrip == image["roundtrip"], "CEL round-trip metrics changed: %s" % path)
    ensure(
        decoded_semantic_sha256(decoded) == cel["decoded_semantic_sha256"],
        "CEL decoded pixels changed: %s" % path,
    )
    ensure(
        decoded_visible_color_count(decoded) == int(selection["visible_target_colors"]),
        "target color count changed: %s" % path,
    )
    return {
        "baseline_size": baseline_size,
        "selected_size": expected_size,
        "coded": int(expected_coded),
        "ccb_ccbpre": int(bool(encoding["ccb_ccbpre"])),
        "bpp": expected_bpp,
    }


def verify_effect_composites(
    source_directory: Path,
    images: Sequence[Mapping[str, Any]],
    decoded: Mapping[str, Path],
) -> None:
    parsed = parse_path(source_directory / "MISC.VOL")
    records = {str(entry["role"]): entry for entry in parsed["records"]}
    frames = records["effects.frames"]["metadata"]["frames"]
    source_entries: Dict[int, Mapping[str, Any]] = {}
    composite_entries: Dict[Tuple[int, str], Mapping[str, Any]] = {}
    for image in images:
        source_match = re.search(
            r"(?:^|_)image_blob_(\d+)$",
            str(image["name"]),
        )
        if source_match:
            source_entries[int(source_match.group(1))] = image
        if "effect_composite_frame" in image:
            composite_entries[
                (
                    int(image["effect_composite_frame"]),
                    str(image["effect_composite_direction"]),
                )
            ] = image

    ensure(
        len(composite_entries) == len(EFFECT_COMPOSITE_FRAMES) * 2,
        "effect composite set is incomplete",
    )
    for frame_index in EFFECT_COMPOSITE_FRAMES:
        frame = frames[frame_index]
        for direction_name, flipped in (("normal", False), ("flipped", True)):
            layers: List[Dict[str, Any]] = []
            for reference in frame["images"]:
                image_index = int(reference["image"])
                ensure(
                    image_index in source_entries,
                    "effect composite verifier is missing image %d"
                    % image_index,
                )
                source_entry = source_entries[image_index]
                source_name = str(source_entry["name"])
                ensure(
                    source_name in decoded,
                    "effect composite source was not decoded: %s"
                    % source_name,
                )
                width, height, pixels = read_png_rgba(decoded[source_name])
                layers.append(
                    {
                        "x": int(
                            reference[
                                "x_flipped" if flipped else "x_normal"
                            ]
                        ),
                        "y": int(reference["y"]),
                        "width": width,
                        "height": height,
                        "pixels": pixels,
                    }
                )
            minimum_x = min(int(layer["x"]) for layer in layers)
            minimum_y = min(int(layer["y"]) for layer in layers)
            maximum_x = max(
                int(layer["x"]) + int(layer["width"]) for layer in layers
            )
            maximum_y = max(
                int(layer["y"]) + int(layer["height"]) for layer in layers
            )
            width = maximum_x - minimum_x
            height = maximum_y - minimum_y
            expected = bytearray(b"\x00\x00\x00\x00") * (width * height)
            for layer in layers:
                layer_width = int(layer["width"])
                layer_height = int(layer["height"])
                layer_pixels = bytes(layer["pixels"])
                for source_y in range(layer_height):
                    for source_x in range(layer_width):
                        source_offset = (
                            source_y * layer_width + source_x
                        ) * 4
                        if layer_pixels[source_offset + 3] == 0:
                            continue
                        draw_x = (
                            layer_width - 1 - source_x
                            if flipped
                            else source_x
                        )
                        target_x = (
                            int(layer["x"]) - minimum_x + draw_x
                        )
                        target_y = int(layer["y"]) - minimum_y + source_y
                        target_offset = (
                            target_y * width + target_x
                        ) * 4
                        expected[target_offset : target_offset + 4] = (
                            layer_pixels[source_offset : source_offset + 4]
                        )

            composite = composite_entries[(frame_index, direction_name)]
            composite_name = str(composite["name"])
            ensure(
                int(composite["effect_composite_origin_x"]) == minimum_x
                and int(composite["effect_composite_origin_y"]) == minimum_y,
                "effect composite origin changed: %s" % composite_name,
            )
            actual_width, actual_height, actual = read_png_rgba(
                decoded[composite_name]
            )
            ensure(
                (actual_width, actual_height) == (width, height),
                "effect composite dimensions changed: %s" % composite_name,
            )
            for offset in range(0, len(actual), 4):
                expected_alpha = expected[offset + 3]
                actual_alpha = actual[offset + 3]
                ensure(
                    expected_alpha == actual_alpha,
                    "effect composite alpha differs: %s" % composite_name,
                )
                if expected_alpha != 0:
                    ensure(
                        expected[offset : offset + 3]
                        == actual[offset : offset + 3],
                        "effect composite color differs: %s"
                        % composite_name,
                    )


def verify_scene_layer1_composite(
    images: Sequence[Mapping[str, Any]],
    decoded: Mapping[str, Path],
) -> None:
    entries = {str(image["name"]): image for image in images}
    names = ("scene_layer1a", "scene_layer1b", "scene_layer1")
    ensure(
        all(name in entries and name in decoded for name in names),
        "scene layer1 composite set is incomplete",
    )
    top_width, top_height, top = read_png_rgba(decoded["scene_layer1a"])
    bottom_width, bottom_height, bottom = read_png_rgba(
        decoded["scene_layer1b"]
    )
    width, height, actual = read_png_rgba(decoded["scene_layer1"])
    ensure(
        top_width == bottom_width == width
        and top_height == bottom_height == 100
        and height == 200,
        "scene layer1 composite dimensions changed",
    )
    expected = top + bottom
    for offset in range(0, len(actual), 4):
        ensure(
            actual[offset + 3] == expected[offset + 3],
            "scene layer1 composite alpha differs",
        )
        if expected[offset + 3] != 0:
            ensure(
                actual[offset : offset + 3]
                == expected[offset : offset + 3],
                "scene layer1 composite color differs",
            )


def verify_release(manifest_path: Path, image_tool: str) -> Dict[str, int]:
    manifest = load_manifest(manifest_path)
    ensure(manifest["format"] == "nog2-3do-assets-v4", "unknown 3DO asset manifest")
    ensure(
        manifest["image_tool"] == image_tool_identity(image_tool),
        "3it binary differs from the asset manifest",
    )
    images = 0
    audio = 0
    baseline_cel_bytes = 0
    selected_cel_bytes = 0
    coded_images = 0
    ccbpre_images = 0
    bpp_counts: Dict[str, int] = {}
    with tempfile.TemporaryDirectory(prefix="nog2-cel-verify-") as temporary:
        decoded_root = Path(temporary)
        for file_entry in manifest["files"]:
            decoded_images: Dict[str, Path] = {}
            for image in file_entry["images"]:
                decoded_path = decoded_root / ("%04d.png" % images)
                result = verify_image_entry(
                    image,
                    decoded_path,
                    image_tool,
                )
                decoded_images[str(image["name"])] = decoded_path
                baseline_cel_bytes += result["baseline_size"]
                selected_cel_bytes += result["selected_size"]
                coded_images += result["coded"]
                ccbpre_images += result["ccb_ccbpre"]
                bpp_key = str(result["bpp"])
                bpp_counts[bpp_key] = bpp_counts.get(bpp_key, 0) + 1
                images += 1
            if file_entry["name"] == "MISC.VOL":
                verify_effect_composites(
                    Path(str(manifest["source_directory"])),
                    file_entry["images"],
                    decoded_images,
                )
            elif file_entry["name"] == "NOGGINBG.VOL":
                verify_scene_layer1_composite(
                    file_entry["images"],
                    decoded_images,
                )
            for sound in file_entry["audio"]:
                path = Path(str(sound["aiff"]["path"]))
                content = path.read_bytes()
                ensure(
                    len(content) == int(sound["aiff"]["size"]),
                    "AIFF size changed: %s" % path,
                )
                ensure(
                    sha256_bytes(content) == sound["aiff"]["sha256"],
                    "AIFF hash changed: %s" % path,
                )
                source = Path(str(sound["source_aiff"]))
                ensure(
                    content == source.read_bytes(),
                    "target AIFF differs from normalized source: %s" % path,
                )
                audio += 1
    ensure(images == int(manifest["image_count"]), "3DO image count changed")
    ensure(audio == int(manifest["audio_count"]), "3DO audio count changed")
    policy = manifest["cel_policy"]
    ensure(policy["name"] == CEL_SELECTION_POLICY, "unknown release CEL policy")
    ensure(policy["packed"] is True, "release CEL policy is not packed")
    ensure(int(policy["baseline_bytes"]) == baseline_cel_bytes, "baseline CEL bytes changed")
    ensure(int(policy["selected_bytes"]) == selected_cel_bytes, "selected CEL bytes changed")
    ensure(
        int(policy["saved_bytes"]) == baseline_cel_bytes - selected_cel_bytes,
        "release CEL savings changed",
    )
    ensure(int(policy["coded_images"]) == coded_images, "coded CEL count changed")
    ensure(int(policy["uncoded_images"]) == images - coded_images, "uncoded CEL count changed")
    ensure(policy["bpp_counts"] == bpp_counts, "CEL bpp counts changed")
    contract = manifest["runtime_contract"]
    ensure(contract["name"] == CEL_RUNTIME_CONTRACT, "unknown CEL runtime contract")
    ensure(
        contract["selected_cel_pixc"] == "0x%08x" % PIXC_OPAQUE,
        "release CEL PIXC contract changed",
    )
    ensure(
        contract["coded_cel_requires_ccb_ldplut"] is True,
        "release coded CEL PLUT contract changed",
    )
    ccbpre = contract["ccb_ccbpre"]
    ensure(ccbpre["required"] is False, "release unnecessarily requires CCB_CCBPRE")
    ensure(int(ccbpre["set_images"]) == ccbpre_images, "CCB_CCBPRE count changed")
    ensure(
        int(ccbpre["unset_images"]) == images - ccbpre_images,
        "non-CCB_CCBPRE count changed",
    )
    return {
        "files": len(manifest["files"]),
        "images": images,
        "audio": audio,
        "coded": coded_images,
        "ccb_ccbpre": ccbpre_images,
        "saved_bytes": baseline_cel_bytes - selected_cel_bytes,
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    convert = subparsers.add_parser("convert")
    convert.add_argument("input_directory")
    convert.add_argument("normalized_directory")
    convert.add_argument("target_directory")
    convert.add_argument("--3it", default="3it", dest="image_tool")
    convert_export_parser = subparsers.add_parser("convert-export")
    convert_export_parser.add_argument("normalized_directory")
    convert_export_parser.add_argument("target_directory")
    convert_export_parser.add_argument(
        "--3it", default="3it", dest="image_tool"
    )
    verify = subparsers.add_parser("verify")
    verify.add_argument("manifest")
    verify.add_argument("--3it", default="3it", dest="image_tool")
    return parser


def main(argv: Sequence[str]) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        if args.command == "convert":
            result = convert_release(
                Path(args.input_directory),
                Path(args.normalized_directory),
                Path(args.target_directory),
                str(args.image_tool),
            )
            print(
                "converted %d files, %d CELs, and %d AIFFs to %s"
                % (
                    result["file_count"],
                    result["image_count"],
                    result["audio_count"],
                    args.target_directory,
                )
            )
        elif args.command == "convert-export":
            result = convert_export(
                Path(args.normalized_directory),
                Path(args.target_directory),
                str(args.image_tool),
            )
            print(
                "converted %d files, %d CELs, and %d AIFFs from %s to %s"
                % (
                    result["file_count"],
                    result["image_count"],
                    result["audio_count"],
                    args.normalized_directory,
                    args.target_directory,
                )
            )
        else:
            counts = verify_release(Path(args.manifest), str(args.image_tool))
            print(
                "verified %d files, %d CELs (%d coded, %d CCB_CCBPRE), "
                "and %d AIFFs; saved %d CEL bytes"
                % (
                    counts["files"],
                    counts["images"],
                    counts["coded"],
                    counts["ccb_ccbpre"],
                    counts["audio"],
                    counts["saved_bytes"],
                )
            )
        return 0
    except (FormatError, OSError, ValueError) as error:
        print("convert_3do: %s" % error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
