#!/usr/bin/env python3
"""Combine UF2 files while preserving block order within each input."""

from __future__ import annotations

import argparse
from pathlib import Path


BLOCK_SIZE = 512
MAGIC_START0 = 0x0A324655
MAGIC_START1 = 0x9E5D5157
MAGIC_END = 0x0AB16F30


def _u32le(block: bytes, offset: int) -> int:
    return int.from_bytes(block[offset:offset + 4], "little")


def _validate_uf2(path: Path, data: bytes) -> None:
    if len(data) == 0 or len(data) % BLOCK_SIZE != 0:
        raise ValueError(f"{path} is not block-aligned UF2 data")
    for offset in range(0, len(data), BLOCK_SIZE):
        block = data[offset:offset + BLOCK_SIZE]
        if (
            _u32le(block, 0) != MAGIC_START0
            or _u32le(block, 4) != MAGIC_START1
            or _u32le(block, 508) != MAGIC_END
        ):
            raise ValueError(f"{path} contains an invalid UF2 block at offset {offset}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("-o", "--output", required=True, type=Path)
    parser.add_argument("inputs", nargs="+", type=Path)
    args = parser.parse_args()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("wb") as output:
        for path in args.inputs:
            data = path.read_bytes()
            _validate_uf2(path, data)
            output.write(data)


if __name__ == "__main__":
    main()
