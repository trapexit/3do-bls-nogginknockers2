"""Lossless raw extraction and normalized image/audio previews for nog2vol."""

from __future__ import annotations

import json
import re
import struct
import zlib
from pathlib import Path
from typing import Any, Dict, List, Mapping, Tuple

from formats import FormatError, ensure, parse_path, parse_rle_image, sha256_bytes


def safe_name(value: str) -> str:
    result = re.sub(r"[^A-Za-z0-9_.-]+", "_", value).strip("_")
    return result or "record"


def write_bytes(path: Path, data: bytes) -> Dict[str, Any]:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return {"path": path.as_posix(), "size": len(data), "sha256": sha256_bytes(data)}


def append_suffix(path: Path, suffix: str) -> Path:
    return path.parent / (path.name + suffix)


def vga_palette_to_rgb8(palette: bytes) -> bytes:
    ensure(len(palette) == 768, "VGA palette must contain 256 RGB triples")
    ensure(max(palette, default=0) <= 63, "VGA palette component exceeds six bits")
    return bytes(((component << 2) | (component >> 4)) for component in palette)


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    checksum = zlib.crc32(kind)
    checksum = zlib.crc32(payload, checksum) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", checksum)


def indexed_png(width: int, height: int, pixels: bytes, palette: bytes, transparent: int | None) -> bytes:
    ensure(width > 0 and height > 0, "PNG dimensions must be positive")
    ensure(len(pixels) == width * height, "PNG index buffer size mismatch")
    rgb_palette = vga_palette_to_rgb8(palette)
    scanlines = b"".join(
        b"\0" + pixels[row * width : (row + 1) * width] for row in range(height)
    )
    result = bytearray(b"\x89PNG\r\n\x1a\n")
    result.extend(png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0)))
    result.extend(png_chunk(b"PLTE", rgb_palette))
    if transparent is not None:
        ensure(0 <= transparent < 256, "transparent palette index is invalid")
        alpha = bytearray([255] * 256)
        alpha[transparent] = 0
        result.extend(png_chunk(b"tRNS", bytes(alpha)))
    result.extend(png_chunk(b"IDAT", zlib.compress(scanlines, level=9)))
    result.extend(png_chunk(b"IEND", b""))
    return bytes(result)


def rgba_png(width: int, height: int, pixels: bytes) -> bytes:
    ensure(width > 0 and height > 0, "PNG dimensions must be positive")
    ensure(len(pixels) == width * height * 4, "PNG RGBA buffer size mismatch")
    scanlines = b"".join(
        b"\0" + pixels[row * width * 4 : (row + 1) * width * 4]
        for row in range(height)
    )
    result = bytearray(b"\x89PNG\r\n\x1a\n")
    result.extend(png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)))
    result.extend(png_chunk(b"IDAT", zlib.compress(scanlines, level=9)))
    result.extend(png_chunk(b"IEND", b""))
    return bytes(result)


def palette_rgba(
    indices: bytes,
    palette: bytes,
    transparent: int | None,
    normalize_for_cel: bool,
    palette_remap: Mapping[int, int] | None = None,
) -> bytes:
    rgb = vga_palette_to_rgb8(palette)
    output = bytearray()
    for index in indices:
        if transparent is not None and index == transparent:
            output.extend((1, 2, 3, 0))
            continue
        color_index = (
            palette_remap.get(index, index)
            if palette_remap is not None else index
        )
        red, green, blue = rgb[color_index * 3 : color_index * 3 + 3]
        if normalize_for_cel and transparent is not None and red == green == blue == 0:
            blue = 8
        output.extend((red, green, blue, 255))
    return bytes(output)


def decode_rle_pixels(payload: bytes, validate_row_offsets: bool = True) -> Tuple[Dict[str, Any], bytes]:
    image = parse_rle_image(payload, 0, len(payload), validate_row_offsets=validate_row_offsets)
    width = int(image["width"])
    height = int(image["height"])
    transparent = int(image["transparent_index"])
    pixels = bytearray([transparent] * (width * height))

    for row, relative_start in enumerate(image["decoded_row_offsets"]):
        position = int(relative_start)
        x = 0
        while x < width:
            control = payload[position]
            position += 1
            count = control & 0x7F if control & 0x80 else control
            if control & 0x80:
                x += count
            else:
                pixels[row * width + x : row * width + x + count] = payload[position : position + count]
                position += count
                x += count
    return image, bytes(pixels)


def wav_from_signed_pcm(samples: bytes, sample_rate: int) -> bytes:
    ensure(sample_rate > 0, "sample rate must be positive")
    unsigned = bytes(((sample + 128) & 0xFF) for sample in samples)
    data_size = len(unsigned)
    fmt = struct.pack("<HHIIHH", 1, 1, sample_rate, sample_rate, 1, 8)
    riff_size = 4 + 8 + len(fmt) + 8 + data_size + (data_size & 1)
    result = bytearray(b"RIFF" + struct.pack("<I", riff_size) + b"WAVE")
    result.extend(b"fmt " + struct.pack("<I", len(fmt)) + fmt)
    result.extend(b"data" + struct.pack("<I", data_size) + unsigned)
    if data_size & 1:
        result.append(0)
    return bytes(result)


def aiff_from_signed_pcm(samples: bytes, sample_rate: int) -> bytes:
    """Build an uncompressed, mono, signed-8-bit AIFF accepted by Portfolio."""

    ensure(sample_rate > 0, "sample rate must be positive")
    exponent = sample_rate.bit_length() - 1
    ensure(exponent < 64, "sample rate is too large for AIFF extended precision")
    extended_rate = struct.pack(
        ">HQ", exponent + 16383, sample_rate << (63 - exponent)
    )
    common = struct.pack(">hIh", 1, len(samples), 8) + extended_rate
    sound = struct.pack(">II", 0, 0) + samples

    def chunk(kind: bytes, payload: bytes) -> bytes:
        padding = b"\0" if len(payload) & 1 else b""
        return kind + struct.pack(">I", len(payload)) + payload + padding

    body = b"AIFF" + chunk(b"COMM", common) + chunk(b"SSND", sound)
    return b"FORM" + struct.pack(">I", len(body)) + body


def record_payload(data: bytes, record: Mapping[str, Any]) -> bytes:
    offset = int(record["offset"])
    size = int(record["size"])
    payload = data[offset : offset + size]
    ensure(len(payload) == size, "record payload is truncated")
    ensure(sha256_bytes(payload) == record["sha256"], "record hash changed during extraction")
    return payload


def find_record(parsed: Mapping[str, Any], role: str) -> Mapping[str, Any]:
    matches = [record for record in parsed["records"] if record["role"] == role]
    ensure(len(matches) == 1, "expected one %s record in %s" % (role, parsed["name"]))
    return matches[0]


def palette_from(path: Path, role: str) -> bytes:
    parsed = parse_path(path)
    data = path.read_bytes()
    return record_payload(data, find_record(parsed, role))


def release_palettes(vol_root: Path) -> Dict[str, bytes]:
    return {
        "NOGGINBG": palette_from(vol_root / "NOGGINBG.VOL", "scene.palette"),
        "NOGTITLE": palette_from(vol_root / "NOGTITLE.VOL", "scene.palette"),
        "SELECT": palette_from(vol_root / "SELECT.VOL", "scene.palette"),
    }


def palette_for_file(
    parsed: Mapping[str, Any],
    data: bytes,
    shared_palettes: Mapping[str, bytes],
) -> Tuple[bytes, str]:
    family = parsed["family"]
    name = str(parsed["name"])
    if family == "scene":
        return record_payload(data, find_record(parsed, "scene.palette")), name
    if family == "cinema":
        return record_payload(data, find_record(parsed, "cinema.palette")), name
    if family == "selector_options":
        return record_payload(data, find_record(parsed, "selector_options.palette")), name
    if family == "selector_marks":
        return shared_palettes["SELECT"], "SELECT.VOL"
    if family in {"font", "character", "effects"}:
        return shared_palettes["NOGGINBG"], "NOGGINBG.VOL preview"
    return bytes(768), "none"


def cinema_sprite_palettes(
    parsed: Mapping[str, Any],
    data: bytes,
    base_palette: bytes,
) -> Dict[int, Tuple[bytes, str]]:
    """Resolve each live cinema sprite image through the active DOS palette."""

    if parsed["family"] != "cinema":
        return {}
    name = str(parsed["name"])
    event_record = find_record(parsed, "cinema.events")
    sprite_record = find_record(parsed, "cinema.sprite_buffer")
    sprites = {
        int(sprite["index"]): sprite
        for sprite in sprite_record["metadata"]["sprites"]
    }
    current_palette = base_palette
    current_source = name
    result: Dict[int, Tuple[bytes, str]] = {}

    for event in event_record["metadata"]["entries"]:
        event_type = int(event["type"])
        if event_type == 2:
            background_index = int(event["data"])
            background_role = "cinema.image[%d]" % background_index
            background = record_payload(
                data, find_record(parsed, background_role)
            )
            if len(background) >= 320 * 120 + 768:
                current_palette = background[320 * 120:320 * 120 + 768]
                current_source = background_role + " appended palette"
        elif event_type == 3:
            sprite_index = int(event["data"]) & 0x7F
            ensure(
                sprite_index in sprites,
                "%s event references missing sprite %d" % (name, sprite_index),
            )
            for frame in sprites[sprite_index]["frames"]:
                for image in frame["images"]:
                    image_index = int(image["image"])
                    if image_index in result:
                        previous_palette, previous_source = result[image_index]
                        ensure(
                            previous_palette == current_palette,
                            "%s cinema image %d needs conflicting palettes %s and %s"
                            % (
                                name,
                                image_index,
                                previous_source,
                                current_source,
                            ),
                        )
                    else:
                        result[image_index] = (
                            current_palette,
                            current_source,
                        )
    return result


def image_output(
    output_directory: Path,
    stem: str,
    payload: bytes,
    palette: bytes,
    palette_source: str,
    validate_row_offsets: bool,
    source: Mapping[str, Any],
    palette_remap: Mapping[int, int] | None = None,
) -> Dict[str, Any]:
    image, pixels = decode_rle_pixels(payload, validate_row_offsets=validate_row_offsets)
    base = output_directory / "images" / safe_name(stem)
    idx = write_bytes(append_suffix(base, ".idx"), pixels)
    if palette_remap is None:
        preview = indexed_png(
            int(image["width"]),
            int(image["height"]),
            pixels,
            palette,
            int(image["transparent_index"]),
        )
    else:
        preview = rgba_png(
            int(image["width"]),
            int(image["height"]),
            palette_rgba(
                pixels,
                palette,
                int(image["transparent_index"]),
                False,
                palette_remap,
            ),
        )
    png = write_bytes(append_suffix(base, ".png"), preview)
    rgba = palette_rgba(
        pixels,
        palette,
        int(image["transparent_index"]),
        normalize_for_cel=True,
        palette_remap=palette_remap,
    )
    normalized_png = write_bytes(
        append_suffix(base, ".rgba.png"),
        rgba_png(int(image["width"]), int(image["height"]), rgba),
    )
    result = {
        "kind": "indexed_rle_image",
        "name": stem,
        "source": dict(source),
        "width": image["width"],
        "height": image["height"],
        "transparent_index": image["transparent_index"],
        "transparent": True,
        "transparent_rgba": "0x01020300",
        "visible_black_remap_rgba": "0x000008ff",
        "palette_source": palette_source,
        "indexed": idx,
        "png": png,
        "normalized_png": normalized_png,
    }
    if palette_remap is not None:
        result["palette_remap"] = {
            "%02x" % source_index: "%02x" % target_index
            for source_index, target_index in sorted(palette_remap.items())
        }
    return result


def raw_background_output(
    output_directory: Path,
    stem: str,
    payload: bytes,
    base_palette: bytes,
    palette_source: str,
    source: Mapping[str, Any],
) -> Dict[str, Any]:
    ensure(len(payload) >= 320 * 120, "cinema background is truncated")
    pixels = payload[: 320 * 120]
    palette = base_palette
    if len(payload) >= 320 * 120 + 768:
        palette = payload[320 * 120 : 320 * 120 + 768]
        palette_source = stem + " appended palette"
    base = output_directory / "images" / safe_name(stem + "_background")
    idx = write_bytes(append_suffix(base, ".idx"), pixels)
    png = write_bytes(append_suffix(base, ".png"), indexed_png(320, 120, pixels, palette, None))
    normalized_png = write_bytes(
        append_suffix(base, ".rgba.png"),
        rgba_png(320, 120, palette_rgba(pixels, palette, None, False)),
    )
    return {
        "kind": "indexed_raw_background",
        "name": stem,
        "source": dict(source),
        "width": 320,
        "height": 120,
        "transparent": False,
        "palette_source": palette_source,
        "indexed": idx,
        "png": png,
        "normalized_png": normalized_png,
    }


def decode_layer_rle(
    payload: bytes,
    row_offsets: bytes,
) -> Tuple[int, int, bytes, bytes]:
    ensure(len(payload) >= 4, "scene RLE layer is missing its dimensions")
    width, height = struct.unpack_from("<HH", payload, 0)
    ensure(width > 0 and height > 0, "scene RLE layer dimensions are invalid")
    ensure(len(row_offsets) == height * 2, "scene RLE row-offset count is invalid")
    indices = bytearray(width * height)
    alpha = bytearray(width * height)
    for row in range(height):
        position = struct.unpack_from("<H", row_offsets, row * 2)[0]
        ensure(4 <= position < len(payload), "scene RLE row offset is outside its layer")
        x = 0
        while x < width:
            ensure(position < len(payload), "scene RLE row is truncated")
            control = payload[position]
            position += 1
            count = control & 0x7F if control & 0x80 else control
            ensure(count > 0, "scene RLE contains a zero-length run")
            ensure(x + count <= width, "scene RLE run crosses its row")
            if control & 0x80:
                x += count
                continue
            ensure(position + count <= len(payload), "scene RLE literal is truncated")
            start = row * width + x
            indices[start : start + count] = payload[position : position + count]
            alpha[start : start + count] = b"\xff" * count
            position += count
            x += count
    return width, height, bytes(indices), bytes(alpha)


def transparent_layer_output(
    output_directory: Path,
    stem: str,
    payload: bytes,
    row_offsets: bytes,
    palette: bytes,
    palette_source: str,
    source: Mapping[str, Any],
) -> Dict[str, Any]:
    width, height, indices, alpha = decode_layer_rle(payload, row_offsets)
    rgb = vga_palette_to_rgb8(palette)
    preview = bytearray()
    normalized = bytearray()
    for offset, index in enumerate(indices):
        if alpha[offset] == 0:
            preview.extend((0, 0, 0, 0))
            normalized.extend((1, 2, 3, 0))
        else:
            red, green, blue = rgb[index * 3 : index * 3 + 3]
            preview.extend((red, green, blue, 255))
            if red == green == blue == 0:
                blue = 8
            normalized.extend((red, green, blue, 255))
    base = output_directory / "images" / safe_name(stem)
    idx = write_bytes(append_suffix(base, ".idx"), indices)
    alpha_output = write_bytes(append_suffix(base, ".alpha"), alpha)
    png = write_bytes(append_suffix(base, ".png"), rgba_png(width, height, bytes(preview)))
    normalized_png = write_bytes(
        append_suffix(base, ".rgba.png"), rgba_png(width, height, bytes(normalized))
    )
    return {
        "kind": "indexed_layer_rle",
        "name": stem,
        "source": dict(source),
        "width": width,
        "height": height,
        "transparent": True,
        "transparent_rgba": "0x01020300",
        "visible_black_remap_rgba": "0x000008ff",
        "palette_source": palette_source,
        "indexed": idx,
        "alpha": alpha_output,
        "png": png,
        "normalized_png": normalized_png,
    }


def raw_layer_output(
    output_directory: Path,
    stem: str,
    payload: bytes,
    palette: bytes,
    palette_source: str,
    source: Mapping[str, Any],
) -> Dict[str, Any]:
    ensure(len(payload) > 0 and len(payload) % 320 == 0, "raw scene layer size is invalid")
    width = 320
    height = len(payload) // width
    base = output_directory / "images" / safe_name(stem)
    idx = write_bytes(append_suffix(base, ".idx"), payload)
    png = write_bytes(append_suffix(base, ".png"), indexed_png(width, height, payload, palette, None))
    normalized_png = write_bytes(
        append_suffix(base, ".rgba.png"),
        rgba_png(width, height, palette_rgba(payload, palette, None, False)),
    )
    return {
        "kind": "indexed_raw_layer",
        "name": stem,
        "source": dict(source),
        "width": width,
        "height": height,
        "transparent": False,
        "palette_source": palette_source,
        "indexed": idx,
        "png": png,
        "normalized_png": normalized_png,
    }


def extract_parsed_file(
    path: Path,
    output_root: Path,
    shared_palettes: Mapping[str, bytes],
) -> Dict[str, Any]:
    parsed = parse_path(path)
    data = path.read_bytes()
    destination = output_root / path.stem.upper()
    palette, palette_source = palette_for_file(parsed, data, shared_palettes)
    cinema_palettes = cinema_sprite_palettes(parsed, data, palette)
    extracted_records: List[Dict[str, Any]] = []
    images: List[Dict[str, Any]] = []
    audio: List[Dict[str, Any]] = []

    for ordinal, record in enumerate(parsed["records"]):
        payload = record_payload(data, record)
        raw_name = "%04d_%s.bin" % (ordinal, safe_name(str(record["role"])))
        raw_output = write_bytes(destination / "records" / raw_name, payload)
        extracted_record = {
            "role": record["role"],
            "source_offset": record["offset"],
            "source_size": record["size"],
            "source_sha256": record["sha256"],
            "output": raw_output,
        }
        if "metadata" in record:
            extracted_record["metadata"] = record["metadata"]
        extracted_records.append(extracted_record)

        metadata = record.get("metadata", {})
        role = str(record["role"])
        if metadata.get("sample_format") == "signed 8-bit mono PCM":
            played = int(metadata.get("dos_played_samples", len(payload)))
            ensure(0 <= played <= len(payload), "played PCM range is invalid")
            audio_entry: Dict[str, Any] = {
                "role": role,
                "stored_samples": len(payload),
                "played_samples": played,
                "sample_rate_hz": metadata["sample_rate_hz"],
            }
            if payload:
                stem = safe_name(role)
                audio_entry["source_pcm"] = write_bytes(destination / "audio" / (stem + ".s8"), payload)
                audio_entry["wav"] = write_bytes(
                    destination / "audio" / (stem + ".wav"),
                    wav_from_signed_pcm(payload[:played], int(metadata["sample_rate_hz"])),
                )
                audio_entry["aiff"] = write_bytes(
                    destination / "audio" / (stem + ".aiff"),
                    aiff_from_signed_pcm(payload[:played], int(metadata["sample_rate_hz"])),
                )
            audio.append(audio_entry)

        roles = metadata.get("roles", [])
        if any("background" in str(item) for item in roles) and payload:
            source = {"role": role, "offset": record["offset"], "size": record["size"], "sha256": record["sha256"]}
            images.append(raw_background_output(destination, role, payload, palette, palette_source, source))

        if "image" in metadata and payload:
            source = {"role": role, "offset": record["offset"], "size": record["size"], "sha256": record["sha256"]}
            image_palette = palette
            image_palette_source = palette_source
            if parsed["family"] == "cinema":
                image_index = int(metadata["index"])
                if image_index in cinema_palettes:
                    image_palette, image_palette_source = cinema_palettes[
                        image_index
                    ]
            images.append(
                image_output(
                    destination,
                    role,
                    payload,
                    image_palette,
                    image_palette_source,
                    True,
                    source,
                )
            )

        if role == "font.glyph_area":
            for glyph_ordinal, member in enumerate(metadata["members"]):
                start = int(member["offset"])
                end = start + int(member["size"])
                glyph = payload[start:end]
                label = "glyph_%s" % "_".join("%03d" % value for value in member["characters"])
                source = {
                    "role": role,
                    "offset": int(record["offset"]) + start,
                    "size": len(glyph),
                    "sha256": member["sha256"],
                }
                base_image = image_output(
                    destination, label, glyph, palette,
                    palette_source, False, source
                )
                base_image["font_glyph_ordinal"] = glyph_ordinal
                base_image["font_variant"] = "base"
                images.append(base_image)
                if str(parsed["name"]).upper() == "FONT3.VOL":
                    variants = (
                        ("white", {0xCA: 0xFF, 0xDB: 0x00}),
                        ("gold", {0xCA: 0xCD, 0xDB: 0xDF}),
                        ("icer", {0xCA: 0x89, 0xDB: 0x00}),
                        ("stump", {0xCA: 0x32, 0xDB: 0x00}),
                    )
                    for variant_name, remap in variants:
                        variant = image_output(
                            destination,
                            label + "_" + variant_name,
                            glyph,
                            palette,
                            palette_source,
                            False,
                            source,
                            remap,
                        )
                        variant["font_glyph_ordinal"] = glyph_ordinal
                        variant["font_variant"] = variant_name
                        images.append(variant)

        if role.endswith("image_blob"):
            for member in metadata.get("members", []):
                start = int(member["offset"])
                end = start + int(member["size"])
                image_payload = payload[start:end]
                label = "%s_%03d" % (role.replace(".", "_"), int(member["index"]))
                source = {
                    "role": role,
                    "index": member["index"],
                    "offset": int(record["offset"]) + start,
                    "size": len(image_payload),
                    "sha256": member["sha256"],
                }
                base_image = image_output(
                    destination, label, image_payload,
                    palette, palette_source, True, source
                )
                images.append(base_image)
                if (str(parsed["name"]).upper() == "SELECT.VOL"
                        and role == "scene.image_blob"):
                    grayscale_remap = {
                        palette_index: 26 - (
                            sum(palette[palette_index * 3 : palette_index * 3 + 3])
                            * 16 // (256 * 3)
                        )
                        for palette_index in range(256)
                    }
                    gray = image_output(
                        destination,
                        label + "_gray",
                        image_payload,
                        palette,
                        palette_source,
                        True,
                        source,
                        grayscale_remap,
                    )
                    gray["scene_image_index"] = int(member["index"])
                    gray["scene_variant"] = "gray"
                    images.append(gray)

    if parsed["family"] == "scene":
        by_role = {str(record["role"]): record for record in parsed["records"]}
        layer_pairs = (
            ("scene.layer1a", "scene.layer1a_row_offsets"),
            ("scene.layer1b", "scene.layer1b_row_offsets"),
            ("scene.layer2", "scene.layer2_row_offsets"),
        )
        for layer_role, row_role in layer_pairs:
            layer_record = by_role[layer_role]
            row_record = by_role[row_role]
            layer_payload = record_payload(data, layer_record)
            row_payload = record_payload(data, row_record)
            source = {
                "role": layer_role,
                "offset": layer_record["offset"],
                "size": layer_record["size"],
                "sha256": layer_record["sha256"],
                "row_offsets_role": row_role,
                "row_offsets_sha256": row_record["sha256"],
            }
            images.append(
                transparent_layer_output(
                    destination,
                    layer_role.replace(".", "_"),
                    layer_payload,
                    row_payload,
                    palette,
                    palette_source,
                    source,
                )
            )
        layer_record = by_role["scene.layer3"]
        layer_payload = record_payload(data, layer_record)
        source = {
            "role": "scene.layer3",
            "offset": layer_record["offset"],
            "size": layer_record["size"],
            "sha256": layer_record["sha256"],
        }
        images.append(
            raw_layer_output(
                destination,
                "scene_layer3",
                layer_payload,
                palette,
                palette_source,
                source,
            )
        )

    file_manifest = {
        "format": "nog2-extracted-file-v1",
        "source": {
            "path": path.as_posix(),
            "name": parsed["name"],
            "family": parsed["family"],
            "size": parsed["size"],
            "sha256": parsed["sha256"],
        },
        "records": extracted_records,
        "images": images,
        "audio": audio,
    }
    manifest_path = destination / "manifest.json"
    manifest_bytes = (json.dumps(file_manifest, indent=2, sort_keys=True) + "\n").encode("utf-8")
    write_bytes(manifest_path, manifest_bytes)
    return {
        "name": parsed["name"],
        "family": parsed["family"],
        "record_count": len(extracted_records),
        "image_count": len(images),
        "audio_count": len(audio),
        "nonempty_audio_count": sum(1 for item in audio if int(item["stored_samples"]) > 0),
        "manifest": manifest_path.as_posix(),
        "manifest_sha256": sha256_bytes(manifest_bytes),
    }


def extract_release(vol_root: Path, output_root: Path) -> Dict[str, Any]:
    ensure(vol_root.is_dir(), "VOL input directory does not exist")
    output_root.mkdir(parents=True, exist_ok=True)
    palettes = release_palettes(vol_root)
    files: List[Dict[str, Any]] = []
    for path in sorted(vol_root.glob("*.VOL"), key=lambda item: item.name.casefold()):
        files.append(extract_parsed_file(path, output_root, palettes))
    result = {
        "format": "nog2-extracted-release-v1",
        "source_directory": vol_root.as_posix(),
        "file_count": len(files),
        "record_count": sum(int(item["record_count"]) for item in files),
        "image_count": sum(int(item["image_count"]) for item in files),
        "audio_count": sum(int(item["audio_count"]) for item in files),
        "nonempty_audio_count": sum(int(item["nonempty_audio_count"]) for item in files),
        "files": files,
    }
    manifest = output_root / "manifest.json"
    manifest.write_bytes((json.dumps(result, indent=2, sort_keys=True) + "\n").encode("utf-8"))
    return result
