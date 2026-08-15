#!/usr/bin/env python3
"""Generate the small, deterministic Minecraft fixtures used by shell tests.

The integration suite must not depend on a developer's local world files.  This
script uses only the Python standard library and emits deliberately minimal
Java-edition NBT and Anvil region files with the tags exercised by the tests.
"""

from __future__ import annotations

import argparse
import gzip
import io
import pathlib
import struct
import zlib


TAG_END = 0
TAG_BYTE = 1
TAG_SHORT = 2
TAG_INT = 3
TAG_LONG = 4
TAG_FLOAT = 5
TAG_DOUBLE = 6
TAG_BYTE_ARRAY = 7
TAG_STRING = 8
TAG_LIST = 9
TAG_COMPOUND = 10
TAG_INT_ARRAY = 11
TAG_LONG_ARRAY = 12


def _utf8(value: str) -> bytes:
    encoded = value.encode("utf-8")
    if len(encoded) > 0xFFFF:
        raise ValueError("fixture string is too long for NBT")
    return struct.pack(">H", len(encoded)) + encoded


def _tag(tag_type: int, name: str, payload: bytes) -> bytes:
    return bytes((tag_type,)) + _utf8(name) + payload


def _compound(*children: bytes) -> bytes:
    return b"".join(children) + bytes((TAG_END,))


def _list(element_type: int, elements: list[bytes]) -> bytes:
    return bytes((element_type,)) + struct.pack(">i", len(elements)) + b"".join(elements)


def _string(value: str) -> bytes:
    return _utf8(value)


def _int_array(values: list[int]) -> bytes:
    return struct.pack(">i", len(values)) + b"".join(struct.pack(">i", value) for value in values)


def _long_array(values: list[int]) -> bytes:
    return struct.pack(">i", len(values)) + b"".join(struct.pack(">q", value) for value in values)


def _byte_array(values: list[int]) -> bytes:
    return struct.pack(">i", len(values)) + bytes(value & 0xFF for value in values)


def make_level_nbt() -> bytes:
    """Return an uncompressed Java NBT tree representative of level.dat."""

    player = _compound(
        _tag(TAG_SHORT, "Air", struct.pack(">h", 300)),
        _tag(
            TAG_LIST,
            "Pos",
            _list(
                TAG_DOUBLE,
                [struct.pack(">d", value) for value in (0.5, 64.0, -1.5)],
            ),
        ),
        _tag(TAG_INT_ARRAY, "UUID", _int_array([1, 2, 3, 4])),
        _tag(TAG_BYTE_ARRAY, "TestBytes", _byte_array([-128, -1, 0, 1, 127])),
        _tag(TAG_LONG_ARRAY, "TestLongs", _long_array([1, -2, 3])),
    )
    data_packs = _compound(
        _tag(
            TAG_LIST,
            "Enabled",
            _list(TAG_STRING, [_string("vanilla"), _string("file/bukkit")]),
        )
    )
    data = _compound(
        _tag(TAG_BYTE, "Difficulty", struct.pack(">b", 2)),
        _tag(TAG_INT, "SpawnX", struct.pack(">i", 100)),
        _tag(TAG_LONG, "LastPlayed", struct.pack(">q", 1_742_628_144_000)),
        _tag(TAG_FLOAT, "SpawnAngle", struct.pack(">f", 0.0)),
        _tag(TAG_DOUBLE, "BorderSize", struct.pack(">d", 60_000_000.0)),
        _tag(TAG_STRING, "LevelName", _string("fixture-world")),
        _tag(TAG_COMPOUND, "DataPacks", data_packs),
        _tag(TAG_COMPOUND, "Player", player),
    )
    return _tag(TAG_COMPOUND, "", _compound(_tag(TAG_COMPOUND, "Data", data)))


def _gzip_deterministic(data: bytes) -> bytes:
    output = io.BytesIO()
    with gzip.GzipFile(filename="", mode="wb", fileobj=output, mtime=0) as stream:
        stream.write(data)
    return output.getvalue()


def make_region() -> bytes:
    """Return a standard 4 KiB-sector region with one zlib chunk at (0, 0)."""

    level = _compound(
        _tag(TAG_INT, "xPos", struct.pack(">i", 0)),
        _tag(TAG_INT, "zPos", struct.pack(">i", 0)),
    )
    chunk_nbt = _tag(TAG_COMPOUND, "", _compound(_tag(TAG_COMPOUND, "Level", level)))
    compressed = zlib.compress(chunk_nbt, level=9)
    record_length = len(compressed) + 1
    sector_size = 4096
    required_sectors = (record_length + 4 + sector_size - 1) // sector_size
    if required_sectors != 1:
        raise AssertionError("test chunk unexpectedly exceeds one sector")

    region = bytearray(3 * sector_size)
    struct.pack_into(">I", region, 0, (2 << 8) | required_sectors)
    struct.pack_into(">I", region, sector_size, 1_700_000_000)
    struct.pack_into(">I", region, 2 * sector_size, record_length)
    region[2 * sector_size + 4] = 2  # zlib
    region[2 * sector_size + 5 : 2 * sector_size + 5 + len(compressed)] = compressed
    return bytes(region)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("kind", choices=("level", "region"))
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()

    payload = _gzip_deterministic(make_level_nbt()) if args.kind == "level" else make_region()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(payload)


if __name__ == "__main__":
    main()
