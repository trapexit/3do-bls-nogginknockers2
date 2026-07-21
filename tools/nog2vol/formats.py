"""Strict source-derived parsers for the Noggin Knockers 2 VOL families."""

from __future__ import annotations

import hashlib
import struct
from pathlib import Path
from typing import Any, Dict, Iterable, List, Mapping, MutableMapping, Sequence, Set, Tuple


FONT_FILES: Set[str] = {"FONT.VOL", "FONT2.VOL", "FONT3.VOL"}
CHARACTER_FILES: Set[str] = {
    "BUDDY.VOL",
    "ED.VOL",
    "FETUS.VOL",
    "GONZOLES.VOL",
    "GURDIP.VOL",
    "HEAD.VOL",
    "HENRY.VOL",
    "KLUBBOR.VOL",
    "SINAMMON.VOL",
}
SCENE_FILES: Set[str] = {"NOGTITLE.VOL", "NOGGINBG.VOL", "SELECT.VOL"}
CINEMA_FILES: Set[str] = {"LOGO.VOL", "NCRED.VOL"} | {
    "END%d.VOL" % number for number in range(1, 9)
}
EXPECTED_FILES: Set[str] = (
    FONT_FILES
    | CHARACTER_FILES
    | SCENE_FILES
    | CINEMA_FILES
    | {"MISC.VOL", "SELECT2.VOL", "SELECT3.VOL", "TITLESND.VOL", "TIMBRES.VOL", "GRIP.VOL"}
)

SOURCE_ANCHORS: Mapping[str, str] = {
    "font": "TGRAPH.CPP:DrawLetter/GetLetterWidth",
    "character": "TFUNC.CPP:playerstat::Initialize",
    "effects": "TFUNC.CPP:InitializeEffects",
    "scene": "TFUNC.CPP:bground::ReadBackground",
    "selector_marks": "NOGGIN.CPP:SelectScreen",
    "selector_options": "NOGGIN.CPP:SelectScreen and title.cpp:OptionsScreen",
    "title_speech": "title.cpp:TitleScreen",
    "cinema": "TCIN.CPP:PlayCinema",
    "timbres": "OPL2FM.C:load_patches",
    "grip_driver": "TINPUT.CPP:InstallGrip",
}

MAX_RECORD_SIZE = 64 * 1024 * 1024
GRIP_RELEASE_SIZE = 4224
GRIP_RELEASE_SHA256 = "2a82fbc795a9e6a5c67b6e270ea8d1ae5426238221f6ed6fe03b22b60aad4ab5"
IMAGE_TABLE_COUNT = 256
MOVE_COUNT = 130
MOVE_SIZE = 3
JOY_POSITION_SIZE = 5
FRAME_HEADER_SIZE = 12
FRAME_IMAGE_SIZE = 5
FRAME_RECT_SIZE = 8
FRAME_ATTACHMENT_SIZE = 5
SERIES_SIZE = 10
BG_FRAME_SIZE = 9
EVENT_SIZE = 6
SPRITE_POINTER_SIZE = 4
SPRITE_FRAME_SIZE = 6
SPRITE_IMAGE_SIZE = 6


class FormatError(ValueError):
    """A VOL violates a source-derived size, range, or structure invariant."""


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def ensure(condition: bool, message: str) -> None:
    if not condition:
        raise FormatError(message)


class Cursor:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.position = 0
        self.records: List[Dict[str, Any]] = []

    @property
    def remaining(self) -> int:
        return len(self.data) - self.position

    def take(
        self,
        size: int,
        role: str,
        metadata: MutableMapping[str, Any] | None = None,
    ) -> bytes:
        ensure(size >= 0, "%s has a negative size" % role)
        ensure(size <= MAX_RECORD_SIZE, "%s is implausibly large: %d" % (role, size))
        end = self.position + size
        ensure(end >= self.position, "%s offset overflow" % role)
        ensure(
            end <= len(self.data),
            "%s requests %d bytes with %d remaining at 0x%x"
            % (role, size, self.remaining, self.position),
        )
        payload = self.data[self.position : end]
        record: Dict[str, Any] = {
            "role": role,
            "offset": self.position,
            "size": size,
            "sha256": sha256_bytes(payload),
        }
        if metadata is not None:
            record["metadata"] = metadata
        self.records.append(record)
        self.position = end
        return payload

    def u16(self, role: str) -> int:
        start = self.position
        value = struct.unpack("<H", self.take(2, role))[0]
        self.records[-1]["metadata"] = {"value": value, "encoding": "u16le"}
        ensure(self.position == start + 2, "internal u16 cursor error")
        return value

    def i16(self, role: str) -> int:
        value = struct.unpack("<h", self.take(2, role))[0]
        self.records[-1]["metadata"] = {"value": value, "encoding": "i16le"}
        return value

    def u32(self, role: str) -> int:
        value = struct.unpack("<I", self.take(4, role))[0]
        self.records[-1]["metadata"] = {"value": value, "encoding": "u32le"}
        return value

    def length_prefixed(
        self,
        width: int,
        role: str,
        mixer_trim: int = 0,
        pcm: bool = False,
    ) -> bytes:
        if width == 2:
            size = self.u16(role + ".length")
        elif width == 4:
            size = self.u32(role + ".length")
        else:
            raise FormatError("unsupported length width %d" % width)
        metadata: Dict[str, Any] = {"stored_size": size}
        if mixer_trim or pcm:
            ensure(size == 0 or size >= mixer_trim, "%s is smaller than mixer trim" % role)
            metadata["dos_played_samples"] = 0 if size == 0 else size - mixer_trim
            metadata["dos_ignored_suffix_samples"] = 0 if size == 0 else mixer_trim
            metadata["sample_format"] = "signed 8-bit mono PCM"
            metadata["sample_rate_hz"] = 16000
        return self.take(size, role, metadata)

    def finish(self) -> None:
        ensure(self.position == len(self.data), "%d unexplained bytes remain at 0x%x" % (self.remaining, self.position))
        expected = 0
        for record in self.records:
            ensure(record["offset"] == expected, "record coverage gap before %s" % record["role"])
            expected += int(record["size"])
        ensure(expected == len(self.data), "record coverage does not reach EOF")


def parse_rle_image(
    data: bytes,
    offset: int,
    limit: int,
    validate_row_offsets: bool = True,
) -> Dict[str, Any]:
    ensure(0 <= offset <= limit <= len(data), "invalid image bounds")
    ensure(limit - offset >= 4, "image header is truncated")
    marker, width, height, transparent = data[offset : offset + 4]
    ensure(width > 0 and height > 0, "image has zero dimensions at 0x%x" % offset)
    table_end = offset + 4 + height * 2
    ensure(table_end <= limit, "image row table is truncated at 0x%x" % offset)
    row_offsets = list(struct.unpack_from("<%dH" % height, data, offset + 4))
    minimum_row = 4 + height * 2
    row_ends: List[int] = []
    decoded_row_offsets: List[int] = []
    literal_pixels = 0
    transparent_pixels = 0
    sequential_position = table_end

    for row, relative_row in enumerate(row_offsets):
        if validate_row_offsets:
            ensure(relative_row >= minimum_row, "image row %d points into its header" % row)
            row_position = offset + relative_row
            ensure(row_position < limit, "image row %d starts beyond its record" % row)
            if row:
                ensure(relative_row > row_offsets[row - 1], "image row offsets are not strictly increasing")
        else:
            row_position = sequential_position
        decoded_row_offsets.append(row_position - offset)

        pixels = 0
        while pixels < width:
            ensure(row_position < limit, "image row %d RLE is truncated" % row)
            control = data[row_position]
            row_position += 1
            count = control & 0x7F if control & 0x80 else control
            ensure(count > 0, "image row %d contains a zero-length RLE run" % row)
            ensure(pixels + count <= width, "image row %d RLE exceeds width" % row)
            if control & 0x80:
                transparent_pixels += count
            else:
                ensure(row_position + count <= limit, "image row %d literal run is truncated" % row)
                row_position += count
                literal_pixels += count
            pixels += count
        row_ends.append(row_position - offset)
        sequential_position = row_position

    if validate_row_offsets:
        for row in range(height - 1):
            ensure(row_ends[row] <= row_offsets[row + 1], "image row %d overlaps row %d" % (row, row + 1))

    encoded_size = max(row_ends)
    ensure(encoded_size <= limit - offset, "image RLE extends beyond its record")
    return {
        "marker": marker,
        "width": width,
        "height": height,
        "transparent_index": transparent,
        "stored_row_words": row_offsets,
        "decoded_row_offsets": decoded_row_offsets,
        "row_offsets_validated": validate_row_offsets,
        "encoded_size": encoded_size,
        "literal_pixels": literal_pixels,
        "transparent_pixels": transparent_pixels,
    }


def image_members(blob: bytes, offsets: Sequence[int], sizes: Sequence[int], role: str) -> List[Dict[str, Any]]:
    ensure(len(offsets) == len(sizes), "%s offset/size table length mismatch" % role)
    ranges: List[Tuple[int, int, int]] = []
    for index, (offset, size) in enumerate(zip(offsets, sizes)):
        if size == 0:
            continue
        ensure(offset <= len(blob), "%s image %d offset exceeds blob" % (role, index))
        end = offset + size
        ensure(end >= offset and end <= len(blob), "%s image %d range exceeds blob" % (role, index))
        ranges.append((offset, end, index))

    ranges.sort()
    previous_end = 0
    for offset, end, index in ranges:
        ensure(offset >= previous_end, "%s image %d overlaps a previous image" % (role, index))
        previous_end = end

    members: List[Dict[str, Any]] = []
    for offset, end, index in ranges:
        image = parse_rle_image(blob, offset, end)
        image["stored_size"] = end - offset
        image["padding_size"] = end - offset - int(image["encoded_size"])
        members.append(
            {
                "index": index,
                "offset": offset,
                "size": end - offset,
                "sha256": sha256_bytes(blob[offset:end]),
                "image": image,
            }
        )
    return members


def unpack_u16_array(data: bytes, count: int) -> List[int]:
    ensure(len(data) == count * 2, "u16 table size mismatch")
    return list(struct.unpack("<%dH" % count, data))


def unpack_u32_array(data: bytes, count: int) -> List[int]:
    ensure(len(data) == count * 4, "u32 table size mismatch")
    return list(struct.unpack("<%dI" % count, data))


def parse_frame(
    data: bytes,
    offset: int,
    role: str,
    live_image_ids: Set[int] | None = None,
) -> Dict[str, Any]:
    """Decode one packed animator frame at a frame-blob-relative offset."""

    ensure(offset >= 0, "%s has a negative frame offset" % role)
    ensure(offset + FRAME_HEADER_SIZE <= len(data), "%s frame header is truncated" % role)
    (
        stored_size,
        shadow,
        duration,
        sound_effect,
        parameter_bits,
        energy,
        dx,
        dy,
        tx,
        ty,
    ) = struct.unpack_from("<HBBBHBbbbb", data, offset)
    ensure(
        stored_size >= FRAME_HEADER_SIZE + 3,
        "%s frame at 0x%x is smaller than its header and terminators" % (role, offset),
    )
    end = offset + stored_size
    ensure(end >= offset and end <= len(data), "%s frame at 0x%x exceeds its blob" % (role, offset))

    position = offset + FRAME_HEADER_SIZE
    images: List[Dict[str, Any]] = []
    while True:
        ensure(position < end, "%s frame at 0x%x has no image terminator" % (role, offset))
        image_id = data[position]
        if image_id == 0xFF:
            position += 1
            break
        ensure(
            position + FRAME_IMAGE_SIZE <= end,
            "%s frame at 0x%x has a truncated image reference" % (role, offset),
        )
        image_id, orientation, x_normal, x_flipped, y = struct.unpack_from(
            "<BBbbb", data, position
        )
        if live_image_ids is not None:
            ensure(
                image_id in live_image_ids,
                "%s frame at 0x%x references missing image %d" % (role, offset, image_id),
            )
        images.append(
            {
                "image": image_id,
                "orientation": orientation,
                "x_normal": x_normal,
                "x_flipped": x_flipped,
                "y": y,
            }
        )
        position += FRAME_IMAGE_SIZE

    ensure(position < end, "%s frame at 0x%x has no vulnerable-rectangle count" % (role, offset))
    vulnerable_count = data[position]
    position += 1
    ensure(
        position + vulnerable_count * FRAME_RECT_SIZE <= end,
        "%s frame at 0x%x has truncated vulnerable rectangles" % (role, offset),
    )
    vulnerable: List[Dict[str, int]] = []
    for _index in range(vulnerable_count):
        x1, y1, x2, y2 = struct.unpack_from("<hhhh", data, position)
        vulnerable.append({"x1": x1, "y1": y1, "x2": x2, "y2": y2})
        position += FRAME_RECT_SIZE

    ensure(position < end, "%s frame at 0x%x has no attack-rectangle count" % (role, offset))
    attack_count = data[position]
    position += 1
    ensure(
        position + attack_count * FRAME_RECT_SIZE <= end,
        "%s frame at 0x%x has truncated attack rectangles" % (role, offset),
    )
    attack: List[Dict[str, int]] = []
    for _index in range(attack_count):
        x1, y1, x2, y2 = struct.unpack_from("<hhhh", data, position)
        attack.append({"x1": x1, "y1": y1, "x2": x2, "y2": y2})
        position += FRAME_RECT_SIZE

    attachment = None
    if end - position == FRAME_ATTACHMENT_SIZE:
        ensure(
            parameter_bits & 0x0100,
            "%s frame at 0x%x has an attachment trailer without parameter bit 0x0100"
            % (role, offset),
        )
        kind_flags, x, y = struct.unpack_from("<Bhh", data, position)
        attachment = {
            "kind_flags": kind_flags,
            "kind": kind_flags >> 4,
            "variant": kind_flags & 0x0F,
            "x": x,
            "y": y,
        }
        position += FRAME_ATTACHMENT_SIZE
    else:
        ensure(
            position == end,
            "%s frame at 0x%x has %d unexplained trailing bytes"
            % (role, offset, end - position),
        )

    ensure(position == end, "%s frame at 0x%x was not consumed exactly" % (role, offset))
    return {
        "offset": offset,
        "size": stored_size,
        "sha256": sha256_bytes(data[offset:end]),
        "shadow": shadow,
        "duration_100hz": duration,
        "sound_effect_bits": sound_effect,
        "parameter_bits": parameter_bits,
        "energy": energy,
        "dx": dx,
        "dy": dy,
        "tx": tx,
        "ty": ty,
        "images": images,
        "vulnerable_rectangles": vulnerable,
        "attack_rectangles": attack,
        "attachment": attachment,
        "attachment_parameter_set": bool(parameter_bits & 0x0100),
        "references": [],
    }


def unreferenced_ranges(data: bytes, ranges: Sequence[Tuple[int, int]]) -> List[Dict[str, Any]]:
    result: List[Dict[str, Any]] = []
    position = 0
    for start, end in ranges:
        ensure(0 <= start <= end <= len(data), "invalid referenced frame range")
        if position < start:
            result.append(
                {
                    "offset": position,
                    "size": start - position,
                    "sha256": sha256_bytes(data[position:start]),
                }
            )
        position = max(position, end)
    if position < len(data):
        result.append(
            {
                "offset": position,
                "size": len(data) - position,
                "sha256": sha256_bytes(data[position:]),
            }
        )
    return result


def parse_moves_and_frames(
    move_data: bytes,
    frame_data: bytes,
    role: str,
    live_image_ids: Set[int],
) -> Tuple[List[Dict[str, Any]], List[Dict[str, Any]], List[Dict[str, Any]]]:
    ensure(len(move_data) == MOVE_COUNT * MOVE_SIZE, "%s move table size mismatch" % role)
    moves: List[Dict[str, Any]] = []
    frames_by_offset: Dict[int, Dict[str, Any]] = {}

    for move_index in range(MOVE_COUNT):
        first_frame, frame_count = struct.unpack_from("<HB", move_data, move_index * MOVE_SIZE)
        if frame_count == 0:
            ensure(first_frame == 0, "%s empty move %d has a nonzero offset" % (role, move_index))
        else:
            ensure(first_frame < len(frame_data), "%s move %d begins beyond its frame blob" % (role, move_index))

        frame_offsets: List[int] = []
        position = first_frame
        for frame_index in range(frame_count):
            if position not in frames_by_offset:
                frames_by_offset[position] = parse_frame(
                    frame_data, position, role, live_image_ids
                )
            frame = frames_by_offset[position]
            frame["references"].append({"move": move_index, "frame": frame_index})
            frame_offsets.append(position)
            position += int(frame["size"])
        moves.append(
            {
                "index": move_index,
                "first_frame": first_frame,
                "frame_count": frame_count,
                "frame_offsets": frame_offsets,
            }
        )

    ensure(frames_by_offset, "%s contains no referenced frames" % role)
    frames = [frames_by_offset[offset] for offset in sorted(frames_by_offset)]
    ranges: List[Tuple[int, int]] = []
    previous_end = 0
    for frame in frames:
        start = int(frame["offset"])
        end = start + int(frame["size"])
        ensure(start >= previous_end, "%s frame at 0x%x overlaps another move" % (role, start))
        ranges.append((start, end))
        previous_end = end

    unused = unreferenced_ranges(frame_data, ranges)
    return moves, frames, unused


def parse_special_moves(data: bytes, count: int) -> List[Dict[str, Any]]:
    ensure(len(data) == count * 10 * JOY_POSITION_SIZE, "special-move table size mismatch")
    moves: List[Dict[str, Any]] = []
    for move_index in range(count):
        positions: List[Dict[str, int]] = []
        for position_index in range(10):
            offset = (move_index * 10 + position_index) * JOY_POSITION_SIZE
            direction, minimum, maximum = struct.unpack_from("<Bhh", data, offset)
            positions.append(
                {
                    "index": position_index,
                    "direction_bits": direction,
                    "minimum_ticks": minimum,
                    "maximum_ticks": maximum,
                }
            )
        moves.append({"index": move_index, "positions": positions})
    return moves


def parse_throw_data(
    table_data: bytes,
    frame_data: bytes,
    main_moves: Sequence[Mapping[str, Any]],
    main_frames: Sequence[Mapping[str, Any]],
) -> Tuple[List[Dict[str, int]], List[Dict[str, Any]], List[Dict[str, Any]]]:
    ensure(len(table_data) == 4 * 32 * 2, "throw table size mismatch")
    frame_index = {int(frame["offset"]): frame for frame in main_frames}
    pointers: List[Dict[str, int]] = []
    throw_frames_by_offset: Dict[int, Dict[str, Any]] = {}

    for throw_move in range(4):
        main_move = main_moves[93 + throw_move]
        expected_count = sum(
            1
            for offset in main_move["frame_offsets"]
            if int(frame_index[int(offset)]["parameter_bits"]) & 0x0010
        )
        for target in range(32):
            offset = struct.unpack_from("<H", table_data, (throw_move * 32 + target) * 2)[0]
            pointers.append({"throw_move": throw_move, "target": target, "offset": offset})
            if offset == 0:
                continue
            ensure(expected_count > 0, "throw pointer references a move with no throw frames")
            ensure(offset < len(frame_data), "throw pointer begins beyond the throw-frame blob")
            position = offset
            for frame_number in range(expected_count):
                if position not in throw_frames_by_offset:
                    throw_frames_by_offset[position] = parse_frame(
                        frame_data, position, "character.throw_frames"
                    )
                frame = throw_frames_by_offset[position]
                frame["references"].append(
                    {
                        "throw_move": throw_move,
                        "target": target,
                        "frame": frame_number,
                    }
                )
                position += int(frame["size"])

    throw_frames = [throw_frames_by_offset[offset] for offset in sorted(throw_frames_by_offset)]
    ranges: List[Tuple[int, int]] = []
    previous_end = 0
    for frame in throw_frames:
        start = int(frame["offset"])
        end = start + int(frame["size"])
        ensure(start >= previous_end, "throw frame at 0x%x overlaps another sequence" % start)
        ranges.append((start, end))
        previous_end = end
    unused = unreferenced_ranges(frame_data, ranges)
    return pointers, throw_frames, unused


def parse_pain_layout(
    data: bytes,
    live_base_image_ids: Set[int],
    live_pain_image_ids: Set[int],
) -> List[Dict[str, Any]]:
    ensure(len(data) % 50 == 0, "pain layout is not a whole painimage array")
    layouts: List[Dict[str, Any]] = []
    for base_image in range(len(data) // 50):
        images: List[Dict[str, int]] = []
        terminated = False
        for slot in range(8):
            offset = base_image * 50 + slot * FRAME_IMAGE_SIZE
            image_id, orientation, x_normal, x_flipped, y = struct.unpack_from(
                "<BBbbb", data, offset
            )
            if image_id == 0xFF:
                terminated = True
                break
            ensure(base_image in live_base_image_ids, "pain layout exists for a missing base image")
            ensure(image_id in live_pain_image_ids, "pain layout references a missing pain image")
            images.append(
                {
                    "slot": slot,
                    "image": image_id,
                    "orientation": orientation,
                    "x_normal": x_normal,
                    "x_flipped": x_flipped,
                    "y": y,
                }
            )
        ensure(terminated, "pain layout %d has no terminator in its eight runtime slots" % base_image)
        if images:
            layouts.append({"base_image": base_image, "images": images})
    return layouts


def parse_font(data: bytes) -> Tuple[str, List[Dict[str, Any]], Dict[str, Any]]:
    cursor = Cursor(data)
    table = cursor.take(512, "font.offset_table", {"entries": 256, "encoding": "u16le"})
    offsets = unpack_u16_array(table, 256)
    nonzero = sorted(set(offset for offset in offsets if offset))
    ensure(nonzero, "font contains no glyph offsets")
    ensure(nonzero[0] >= 512, "font glyph overlaps its offset table")

    glyph_meta: Dict[str, Any] = {"members": []}
    glyph_area = cursor.take(cursor.remaining, "font.glyph_area", glyph_meta)
    for ordinal, start in enumerate(nonzero):
        limit = nonzero[ordinal + 1] if ordinal + 1 < len(nonzero) else len(data)
        image = parse_rle_image(data, start, limit, validate_row_offsets=False)
        aliases = [index for index, value in enumerate(offsets) if value == start]
        stored_size = limit - start
        glyph_meta["members"].append(
            {
                "characters": aliases,
                "offset": start - 512,
                "size": stored_size,
                "sha256": sha256_bytes(glyph_area[start - 512 : limit - 512]),
                "image": dict(image, padding_size=stored_size - int(image["encoded_size"])),
            }
        )
    glyph_meta["glyph_count"] = len(nonzero)
    cursor.finish()
    return "font", cursor.records, {"glyph_count": len(nonzero)}


def parse_character(data: bytes) -> Tuple[str, List[Dict[str, Any]], Dict[str, Any]]:
    cursor = Cursor(data)
    magic = cursor.take(12, "character.magic", {"expected": "MIDGETPOWER\\0"})
    ensure(magic == b"MIDGETPOWER\0", "invalid character-bank magic")

    for index in range(5):
        role = "character.identity_sound" if index == 0 else "character.select_scream[%d]" % (index - 1)
        cursor.length_prefixed(4, role, mixer_trim=30, pcm=True)
    for index in range(6):
        biography = cursor.length_prefixed(4, "character.biography[%d]" % index)
        if biography:
            image = parse_rle_image(biography, 0, len(biography))
            cursor.records[-1]["metadata"]["image"] = image
            cursor.records[-1]["metadata"]["padding_size"] = len(biography) - int(image["encoded_size"])

    name_bytes = cursor.take(15, "character.legacy_name_bytes")
    stored_name = name_bytes.split(b"\0", 1)[0]
    ensure(bool(stored_name), "character legacy name bytes are empty")
    name_metadata: Dict[str, Any] = {"hex": stored_name.hex()}
    if all(0x20 <= value <= 0x7E for value in stored_name):
        name_metadata["ascii"] = stored_name.decode("ascii")
    cursor.records[-1]["metadata"] = name_metadata
    cursor.take(256, "character.color_map", {"entries": 256})

    offsets = unpack_u32_array(cursor.take(1024, "character.image_offsets"), IMAGE_TABLE_COUNT)
    sizes = unpack_u32_array(cursor.take(1024, "character.image_sizes"), IMAGE_TABLE_COUNT)
    blob_size = cursor.u32("character.image_blob.length")
    blob_metadata: Dict[str, Any] = {}
    blob = cursor.take(blob_size, "character.image_blob", blob_metadata)
    blob_metadata["members"] = image_members(blob, offsets, sizes, "character")
    blob_metadata["image_count"] = len(blob_metadata["members"])
    live_image_ids = {index for index, size in enumerate(sizes) if size}

    for index in range(14):
        cursor.length_prefixed(4, "character.combat_sound[%d]" % index, mixer_trim=30, pcm=True)

    move_metadata: Dict[str, Any] = {"count": MOVE_COUNT, "entry_size": MOVE_SIZE}
    move_bytes = cursor.take(MOVE_COUNT * MOVE_SIZE, "character.moves", move_metadata)
    frame_size = cursor.u16("character.frames.length")
    frame_metadata: Dict[str, Any] = {"header_size": FRAME_HEADER_SIZE}
    frame_bytes = cursor.take(frame_size, "character.frames", frame_metadata)
    moves, frames, unused_frame_ranges = parse_moves_and_frames(
        move_bytes, frame_bytes, "character.frames", live_image_ids
    )
    move_metadata["entries"] = moves
    frame_metadata["frames"] = frames
    frame_metadata["referenced_frame_count"] = len(frames)
    frame_metadata["unreferenced_ranges"] = unused_frame_ranges
    frame_metadata["unreferenced_size"] = sum(int(item["size"]) for item in unused_frame_ranges)

    special_count = cursor.u16("character.special_moves.count")
    ensure(special_count <= 10, "character special-move count exceeds source array")
    special_metadata: Dict[str, Any] = {
        "moves": special_count,
        "positions_per_move": 10,
        "entry_size": JOY_POSITION_SIZE,
    }
    special_bytes = cursor.take(
        special_count * 10 * JOY_POSITION_SIZE,
        "character.special_moves.positions",
        special_metadata,
    )
    special_metadata["entries"] = parse_special_moves(special_bytes, special_count)

    throw_table_metadata: Dict[str, Any] = {"dimensions": [4, 32], "entry_size": 2}
    throw_table_bytes = cursor.take(32 * 4 * 2, "character.throw_table", throw_table_metadata)
    throw_frame_size = cursor.u16("character.throw_frames.length")
    throw_frame_metadata: Dict[str, Any] = {"header_size": FRAME_HEADER_SIZE}
    throw_frame_bytes = cursor.take(
        throw_frame_size, "character.throw_frames", throw_frame_metadata
    )
    throw_pointers, throw_frames, unused_throw_ranges = parse_throw_data(
        throw_table_bytes, throw_frame_bytes, moves, frames
    )
    throw_table_metadata["entries"] = throw_pointers
    throw_table_metadata["nonzero_count"] = sum(
        1 for pointer in throw_pointers if int(pointer["offset"])
    )
    throw_frame_metadata["frames"] = throw_frames
    throw_frame_metadata["referenced_frame_count"] = len(throw_frames)
    throw_frame_metadata["unreferenced_ranges"] = unused_throw_ranges
    throw_frame_metadata["unreferenced_size"] = sum(
        int(item["size"]) for item in unused_throw_ranges
    )

    pain_metadata: Dict[str, Any] = {"members": [], "image_count": 0}
    pain_layout_metadata: Dict[str, Any] = {}
    pain_layout_bytes = b""
    if cursor.remaining:
        pain_layout_size = cursor.u32("character.pain_layout.length")
        ensure(pain_layout_size % 50 == 0, "pain layout is not a whole painimage array")
        pain_layout_metadata = {
            "entry_size": 50,
            "entry_count": pain_layout_size // 50,
            "stored_slots_per_entry": 10,
            "runtime_slots_per_entry": 8,
        }
        pain_layout_bytes = cursor.take(
            pain_layout_size, "character.pain_layout", pain_layout_metadata
        )
        pain_offsets = unpack_u32_array(cursor.take(1024, "character.pain_image_offsets"), IMAGE_TABLE_COUNT)
        pain_sizes = unpack_u32_array(cursor.take(1024, "character.pain_image_sizes"), IMAGE_TABLE_COUNT)
        pain_blob_size = cursor.u32("character.pain_image_blob.length")
        pain_blob = cursor.take(pain_blob_size, "character.pain_image_blob", pain_metadata)
        pain_metadata["members"] = image_members(pain_blob, pain_offsets, pain_sizes, "character.pain")
        pain_metadata["image_count"] = len(pain_metadata["members"])
        pain_layout_metadata["live_entries"] = parse_pain_layout(
            pain_layout_bytes,
            live_image_ids,
            {index for index, size in enumerate(pain_sizes) if size},
        )
        pain_layout_metadata["live_entry_count"] = len(pain_layout_metadata["live_entries"])

    cursor.finish()
    return "character", cursor.records, {
        "legacy_name_bytes_hex": stored_name.hex(),
        "image_count": len(blob_metadata["members"]),
        "pain_image_count": len(pain_metadata["members"]),
        "special_move_count": special_count,
        "live_move_count": sum(1 for move in moves if int(move["frame_count"])),
        "referenced_frame_count": len(frames),
        "unreferenced_frame_bytes": frame_metadata["unreferenced_size"],
        "throw_frame_count": len(throw_frames),
    }


def parse_effects(data: bytes) -> Tuple[str, List[Dict[str, Any]], Dict[str, Any]]:
    cursor = Cursor(data)
    for index in range(5):
        cursor.length_prefixed(2, "effects.identity_sound[%d]" % index)
    for index in range(6):
        cursor.length_prefixed(2, "effects.biography[%d]" % index)
    cursor.take(15, "effects.unused_name")
    cursor.take(256, "effects.color_map", {"entries": 256})

    offsets = unpack_u32_array(cursor.take(1024, "effects.image_offsets"), IMAGE_TABLE_COUNT)
    sizes = unpack_u16_array(cursor.take(512, "effects.image_sizes"), IMAGE_TABLE_COUNT)
    blob_size = cursor.u32("effects.image_blob.length")
    blob_metadata: Dict[str, Any] = {}
    blob = cursor.take(blob_size, "effects.image_blob", blob_metadata)
    blob_metadata["members"] = image_members(blob, offsets, sizes, "effects")
    blob_metadata["image_count"] = len(blob_metadata["members"])
    live_image_ids = {index for index, size in enumerate(sizes) if size}

    for index in range(14):
        cursor.length_prefixed(2, "effects.unused_sound[%d]" % index)
    move_metadata: Dict[str, Any] = {"count": MOVE_COUNT, "entry_size": MOVE_SIZE}
    move_bytes = cursor.take(MOVE_COUNT * MOVE_SIZE, "effects.moves", move_metadata)
    frame_size = cursor.u16("effects.frames.length")
    frame_metadata: Dict[str, Any] = {"header_size": FRAME_HEADER_SIZE}
    frame_bytes = cursor.take(frame_size, "effects.frames", frame_metadata)
    moves, frames, unused_frame_ranges = parse_moves_and_frames(
        move_bytes, frame_bytes, "effects.frames", live_image_ids
    )
    move_metadata["entries"] = moves
    frame_metadata["frames"] = frames
    frame_metadata["referenced_frame_count"] = len(frames)
    frame_metadata["unreferenced_ranges"] = unused_frame_ranges
    frame_metadata["unreferenced_size"] = sum(int(item["size"]) for item in unused_frame_ranges)
    if cursor.remaining:
        cursor.take(
            cursor.remaining,
            "effects.unused_writer_tail",
            {
                "runtime_owner": "none",
                "evidence": "TFUNC.CPP:InitializeEffects closes MISC.VOL immediately after effects.frames",
                "shipping_disposition": "preserve_unused",
            },
        )
    cursor.finish()
    return "effects", cursor.records, {
        "image_count": len(blob_metadata["members"]),
        "live_move_count": sum(1 for move in moves if int(move["frame_count"])),
        "referenced_frame_count": len(frames),
        "unreferenced_frame_bytes": frame_metadata["unreferenced_size"],
        "runtime_consumed_size": len(data) - int(cursor.records[-1]["size"])
        if cursor.records[-1]["role"] == "effects.unused_writer_tail"
        else len(data),
    }


def parse_series(data: bytes, count: int, frame_blob_size: int) -> List[Dict[str, Any]]:
    ensure(len(data) == count * SERIES_SIZE, "series table size mismatch")
    result: List[Dict[str, Any]] = []
    for index in range(count):
        entry = data[index * SERIES_SIZE : (index + 1) * SERIES_SIZE]
        first_frame, frame_count, parameter, saved_pointer, current_frame, duration = struct.unpack(
            "<hBBIBB", entry
        )
        if frame_count:
            ensure(first_frame >= 0, "series %d has a negative first-frame offset" % index)
            ensure(
                first_frame + frame_count * BG_FRAME_SIZE <= frame_blob_size,
                "series %d frame range exceeds the frame blob" % index,
            )
        result.append(
            {
                "index": index,
                "first_frame": first_frame,
                "frame_count": frame_count,
                "parameter": parameter,
                "saved_pointer": saved_pointer,
                "saved_current_frame": current_frame,
                "saved_duration": duration,
            }
        )
    return result


def parse_bg_frames(
    data: bytes,
    series: Sequence[Mapping[str, Any]],
    image_indices: set[int],
) -> Dict[str, Any]:
    referenced: Dict[int, List[Dict[str, int]]] = {}
    for entry in series:
        for frame_index in range(int(entry["frame_count"])):
            offset = int(entry["first_frame"]) + frame_index * BG_FRAME_SIZE
            referenced.setdefault(offset, []).append(
                {"series": int(entry["index"]), "frame": frame_index}
            )

    frames: List[Dict[str, Any]] = []
    occupied = bytearray(len(data))
    for offset in sorted(referenced):
        ensure(offset >= 0 and offset + BG_FRAME_SIZE <= len(data), "background frame is outside its blob")
        size, duration, image_index, x, y = struct.unpack_from("<hhBhh", data, offset)
        ensure(size == BG_FRAME_SIZE, "background frame has an invalid serialized size")
        ensure(duration >= 0, "background frame has a negative duration")
        ensure(image_index == 0xFF or image_index in image_indices, "background frame references a missing image")
        ensure(not any(occupied[offset : offset + BG_FRAME_SIZE]), "background frame ranges overlap")
        occupied[offset : offset + BG_FRAME_SIZE] = b"\x01" * BG_FRAME_SIZE
        frames.append(
            {
                "offset": offset,
                "size": size,
                "duration_100hz": duration,
                "image_index": image_index,
                "x": x,
                "y": y,
                "references": referenced[offset],
            }
        )
    unreferenced = bytes(value for index, value in enumerate(data) if not occupied[index])
    return {
        "entry_size": BG_FRAME_SIZE,
        "referenced_frame_count": len(frames),
        "unreferenced_size": len(unreferenced),
        "unreferenced_sha256": sha256_bytes(unreferenced),
        "frames": frames,
    }


def parse_scene(data: bytes) -> Tuple[str, List[Dict[str, Any]], Dict[str, Any]]:
    cursor = Cursor(data)
    cursor.take(768, "scene.palette", {"entries": 256, "component_bits": 6})
    offsets = unpack_u32_array(cursor.take(320, "scene.image_offsets"), 80)
    sizes = unpack_u16_array(cursor.take(160, "scene.image_sizes"), 80)
    blob_size = cursor.u32("scene.image_blob.length")
    image_metadata: Dict[str, Any] = {}
    image_blob = cursor.take(blob_size, "scene.image_blob", image_metadata)
    image_metadata["members"] = image_members(image_blob, offsets, sizes, "scene")
    image_metadata["image_count"] = len(image_metadata["members"])

    music = cursor.length_prefixed(2, "scene.music")
    if music:
        if music.startswith(b"MThd"):
            music_format = "MIDI"
        elif music.startswith(b"CTMF"):
            music_format = "CMF"
        elif not any(music):
            music_format = "zero_placeholder"
        elif set(music).issubset({0x00, 0xFF}):
            music_format = "binary_placeholder_00_ff"
        else:
            raise FormatError("scene music has an unknown header")
        cursor.records[-1]["metadata"]["format"] = music_format

    series_count_minus_one = cursor.i16("scene.series.max_index")
    ensure(0 <= series_count_minus_one < 40, "scene series count is outside source bounds")
    series_count = series_count_minus_one + 1
    series_metadata: Dict[str, Any] = {"count": series_count, "entry_size": SERIES_SIZE}
    series_bytes = cursor.take(series_count * SERIES_SIZE, "scene.series", series_metadata)
    frame_size = cursor.u16("scene.frames.length")
    frame_metadata: Dict[str, Any] = {}
    frame_bytes = cursor.take(frame_size, "scene.frames", frame_metadata)
    series_entries = parse_series(series_bytes, series_count, len(frame_bytes))
    series_metadata["entries"] = series_entries
    frame_metadata.update(
        parse_bg_frames(
            frame_bytes,
            series_entries,
            {int(member["index"]) for member in image_metadata["members"]},
        )
    )

    cursor.take(200, "scene.layer1a_row_offsets", {"entries": 100, "encoding": "u16le"})
    cursor.take(200, "scene.layer1b_row_offsets", {"entries": 100, "encoding": "u16le"})
    layer2_height = cursor.u16("scene.layer2_height")
    ensure(layer2_height <= 200, "scene layer2 height exceeds logical screen")
    cursor.take(layer2_height * 2, "scene.layer2_row_offsets", {"entries": layer2_height, "encoding": "u16le"})
    for role in ("layer1a", "layer1b", "layer2", "layer3"):
        cursor.length_prefixed(2, "scene.%s" % role)

    if cursor.remaining:
        alternate_size = cursor.u16("scene.awe32_music.declared_length")
        alternate = cursor.take(cursor.remaining, "scene.awe32_music")
        if alternate.startswith(b"MThd"):
            alternate_format = "MIDI"
        elif alternate.startswith(b"CTMF"):
            alternate_format = "CMF"
        elif not any(alternate):
            alternate_format = "zero_placeholder"
        elif set(alternate).issubset({0x00, 0xFF}):
            alternate_format = "binary_placeholder_00_ff"
        else:
            raise FormatError("scene alternate music has an unknown header")
        ensure(alternate_size == len(alternate), "scene alternate music length does not match its tail")
        cursor.records[-1]["metadata"] = {
            "format": alternate_format,
            "declared_length": alternate_size,
            "stored_size": len(alternate),
        }

    cursor.finish()
    return "scene", cursor.records, {
        "image_count": len(image_metadata["members"]),
        "series_count": series_count,
        "referenced_frame_count": sum(
            int(entry["frame_count"]) for entry in series_metadata["entries"]
        ),
        "layer2_height": layer2_height,
    }


def parse_selector_marks(data: bytes) -> Tuple[str, List[Dict[str, Any]], Dict[str, Any]]:
    cursor = Cursor(data)
    images: List[Dict[str, Any]] = []
    for index in range(2):
        payload = cursor.length_prefixed(4, "selector_mark[%d]" % index)
        image = parse_rle_image(payload, 0, len(payload))
        cursor.records[-1]["metadata"]["image"] = image
        cursor.records[-1]["metadata"]["padding_size"] = len(payload) - int(image["encoded_size"])
        images.append(image)
    cursor.finish()
    return "selector_marks", cursor.records, {"image_count": len(images)}


def parse_selector_options(data: bytes) -> Tuple[str, List[Dict[str, Any]], Dict[str, Any]]:
    cursor = Cursor(data)
    cursor.length_prefixed(4, "selector_options.versus_sound", mixer_trim=30, pcm=True)
    cursor.take(768, "selector_options.palette", {"entries": 256, "component_bits": 6})
    for role in ("player1", "player2", "computer"):
        payload = cursor.length_prefixed(4, "selector_options.%s" % role)
        try:
            image = parse_rle_image(payload, 0, len(payload))
        except FormatError:
            image = None
        if image is not None:
            cursor.records[-1]["metadata"]["image"] = image
    for index in range(2):
        cursor.length_prefixed(4, "selector_options.sound[%d]" % index, mixer_trim=30, pcm=True)
    cursor.finish()
    return "selector_options", cursor.records, {}


def parse_title_speech(data: bytes) -> Tuple[str, List[Dict[str, Any]], Dict[str, Any]]:
    cursor = Cursor(data)
    for index in range(5):
        cursor.length_prefixed(4, "title_speech.sound[%d]" % index, mixer_trim=30, pcm=True)
    cursor.finish()
    return "title_speech", cursor.records, {"sound_count": 5}


def parse_sprite_buffer(data: bytes, pointers: Sequence[Tuple[int, int]]) -> Tuple[List[Dict[str, Any]], Set[int]]:
    sprites: List[Dict[str, Any]] = []
    image_ids: Set[int] = set()
    for index, (base, frame_count) in enumerate(pointers):
        if frame_count <= 0:
            continue
        ensure(base < len(data), "cinema sprite %d starts beyond its frame buffer" % index)
        position = base
        frames: List[Dict[str, Any]] = []
        for frame_index in range(frame_count):
            ensure(position + SPRITE_FRAME_SIZE <= len(data), "cinema sprite %d frame header is truncated" % index)
            image_count, duration, dx, dy = struct.unpack_from("<BBhh", data, position)
            frame_size = SPRITE_FRAME_SIZE + image_count * SPRITE_IMAGE_SIZE
            ensure(position + frame_size <= len(data), "cinema sprite %d frame images are truncated" % index)
            images: List[Dict[str, Any]] = []
            image_position = position + SPRITE_FRAME_SIZE
            for image_index in range(image_count):
                image_id, orientation, x, y = struct.unpack_from("<BBhh", data, image_position)
                image_ids.add(image_id)
                images.append({"image": image_id, "orientation": orientation, "x": x, "y": y})
                image_position += SPRITE_IMAGE_SIZE
            frames.append(
                {
                    "index": frame_index,
                    "offset": position,
                    "size": frame_size,
                    "duration": duration,
                    "dx": dx,
                    "dy": dy,
                    "images": images,
                }
            )
            position += frame_size
        sprites.append({"index": index, "base": base, "frame_count": frame_count, "frames": frames})
    return sprites, image_ids


def parse_cinema(data: bytes) -> Tuple[str, List[Dict[str, Any]], Dict[str, Any]]:
    cursor = Cursor(data)
    cursor.take(768, "cinema.palette", {"entries": 256, "component_bits": 6})
    event_count = cursor.i16("cinema.events.count")
    ensure(0 <= event_count <= 32767, "cinema event count is invalid")
    event_metadata: Dict[str, Any] = {"count": event_count, "entry_size": EVENT_SIZE}
    event_bytes = cursor.take(event_count * EVENT_SIZE, "cinema.events", event_metadata)
    events: List[Dict[str, Any]] = []
    background_ids: Set[int] = set()
    sprite_ids: Set[int] = set()
    for index in range(event_count):
        event_type, event_data, x, y = struct.unpack_from("<BBhh", event_bytes, index * EVENT_SIZE)
        ensure(event_type in {0, 1, 2, 3, 4, 5, 6}, "unknown cinema event type %d" % event_type)
        if event_type == 2:
            background_ids.add(event_data)
        elif event_type == 3:
            sprite_ids.add(event_data & 0x7F)
            ensure((event_data & 0x7F) < 50, "cinema sprite event index exceeds table")
        elif event_type == 5:
            ensure(event_data < 26, "cinema sound event index exceeds table")
        elif event_type == 6:
            ensure(event_data < 25, "cinema text event index exceeds table")
        events.append({"index": index, "type": event_type, "data": event_data, "x": x, "y": y})
    event_metadata["entries"] = events

    sprite_buffer_size = cursor.i16("cinema.sprite_buffer.length")
    ensure(sprite_buffer_size >= 0, "cinema sprite buffer length is negative")
    sprite_metadata: Dict[str, Any] = {}
    sprite_buffer = cursor.take(sprite_buffer_size, "cinema.sprite_buffer", sprite_metadata)
    pointer_bytes = cursor.take(50 * SPRITE_POINTER_SIZE, "cinema.sprite_pointers", {"entries": 50})
    pointers: List[Tuple[int, int]] = []
    for index in range(50):
        base, frame_count = struct.unpack_from("<Hh", pointer_bytes, index * SPRITE_POINTER_SIZE)
        ensure(frame_count >= 0, "cinema sprite %d has a negative frame count" % index)
        pointers.append((base, frame_count))
    sprites, sprite_image_ids = parse_sprite_buffer(sprite_buffer, pointers)
    for sprite_id in sprite_ids:
        ensure(pointers[sprite_id][1] > 0, "cinema event references an empty sprite %d" % sprite_id)
    sprite_metadata["sprites"] = sprites
    sprite_metadata["sprite_count"] = len(sprites)

    cursor.take(25 * 100, "cinema.text", {"entries": 25, "entry_size": 100})
    max_image_index = cursor.i16("cinema.images.max_index")
    ensure(0 <= max_image_index < 250, "cinema image max index is outside source bounds")
    image_count = max_image_index + 1
    image_metadata: List[MutableMapping[str, Any]] = []
    image_payloads: List[bytes] = []
    for index in range(image_count):
        payload = cursor.length_prefixed(2, "cinema.image[%d]" % index)
        metadata = cursor.records[-1]["metadata"]
        metadata["index"] = index
        image_metadata.append(metadata)
        image_payloads.append(payload)

    for image_id in background_ids:
        ensure(image_id < image_count, "cinema background event references a missing image")
    for image_id in sprite_image_ids:
        ensure(image_id < image_count, "cinema sprite frame references a missing image")

    for index, (payload, metadata) in enumerate(zip(image_payloads, image_metadata)):
        roles: List[str] = []
        if index in background_ids:
            roles.append("background")
            ensure(len(payload) >= 320 * 120, "cinema background %d is smaller than 320x120" % index)
            metadata["pixel_bytes"] = 320 * 120
            if len(payload) > 320 * 120:
                ensure(len(payload) >= 320 * 120 + 768, "cinema background palette is truncated")
                metadata["appended_palette_bytes"] = 768
                metadata["extra_bytes"] = len(payload) - 320 * 120 - 768
        if index in sprite_image_ids:
            roles.append("sprite")
            image = parse_rle_image(payload, 0, len(payload))
            metadata["image"] = image
            metadata["padding_size"] = len(payload) - int(image["encoded_size"])
        if not roles:
            if len(payload) == 320 * 120 or len(payload) >= 320 * 120 + 768:
                roles.append("unused_background")
                metadata["pixel_bytes"] = 320 * 120
                if len(payload) >= 320 * 120 + 768:
                    metadata["appended_palette_bytes"] = 768
                    metadata["extra_bytes"] = len(payload) - 320 * 120 - 768
            else:
                try:
                    image = parse_rle_image(payload, 0, len(payload))
                except FormatError:
                    roles.append("unused_or_raw")
                else:
                    roles.append("unused_sprite")
                    metadata["image"] = image
                    metadata["padding_size"] = len(payload) - int(image["encoded_size"])
        metadata["roles"] = roles

    for index in range(26):
        cursor.length_prefixed(4, "cinema.sound[%d]" % index, pcm=True)
    cursor.finish()
    return "cinema", cursor.records, {
        "event_count": event_count,
        "sprite_count": len(sprites),
        "image_count": image_count,
        "sound_count": 26,
        "background_image_ids": sorted(background_ids),
        "sprite_image_ids": sorted(sprite_image_ids),
    }


def parse_timbres(data: bytes) -> Tuple[str, List[Dict[str, Any]], Dict[str, Any]]:
    cursor = Cursor(data)
    magic = cursor.take(5, "timbres.magic", {"expected_hex": "4f504c321a"})
    ensure(magic == b"OPL2\x1a", "invalid OPL2 timbre magic")
    count_bytes = cursor.take(1, "timbres.count")
    count = count_bytes[0]
    cursor.records[-1]["metadata"] = {"value": count, "encoding": "u8"}
    patches: List[int] = []
    for index in range(count):
        record = cursor.take(14, "timbres.patch[%d]" % index)
        patches.append(record[0])
        cursor.records[-1]["metadata"] = {"patch": record[0], "timbre_size": 13}
    cursor.finish()
    return "timbres", cursor.records, {"patch_count": count, "patch_ids": patches}


def parse_grip(data: bytes) -> Tuple[str, List[Dict[str, Any]], Dict[str, Any]]:
    ensure(len(data) == GRIP_RELEASE_SIZE, "GrIP payload size differs from the preserved release")
    ensure(sha256_bytes(data) == GRIP_RELEASE_SHA256, "GrIP payload hash differs from the preserved release")
    cursor = Cursor(data)
    cursor.take(len(data), "grip_driver.payload", {"shipping_disposition": "exclude_dos_support"})
    cursor.finish()
    return "grip_driver", cursor.records, {"payload_size": len(data)}


def parse_bytes(name: str, data: bytes) -> Dict[str, Any]:
    upper_name = Path(name).name.upper()
    if upper_name in FONT_FILES:
        family, records, summary = parse_font(data)
    elif upper_name in CHARACTER_FILES:
        family, records, summary = parse_character(data)
    elif upper_name == "MISC.VOL":
        family, records, summary = parse_effects(data)
    elif upper_name in SCENE_FILES:
        family, records, summary = parse_scene(data)
    elif upper_name == "SELECT2.VOL":
        family, records, summary = parse_selector_marks(data)
    elif upper_name == "SELECT3.VOL":
        family, records, summary = parse_selector_options(data)
    elif upper_name == "TITLESND.VOL":
        family, records, summary = parse_title_speech(data)
    elif upper_name in CINEMA_FILES:
        family, records, summary = parse_cinema(data)
    elif upper_name == "TIMBRES.VOL":
        family, records, summary = parse_timbres(data)
    elif upper_name == "GRIP.VOL":
        family, records, summary = parse_grip(data)
    else:
        raise FormatError("unsupported VOL filename: %s" % upper_name)

    return {
        "name": upper_name,
        "family": family,
        "source_anchor": SOURCE_ANCHORS[family],
        "size": len(data),
        "sha256": sha256_bytes(data),
        "consumed": len(data),
        "records": records,
        "summary": summary,
    }


def parse_path(path: Path) -> Dict[str, Any]:
    try:
        data = path.read_bytes()
    except OSError as error:
        raise FormatError("cannot read %s: %s" % (path, error)) from error
    result = parse_bytes(path.name, data)
    result["path"] = path.as_posix()
    return result
