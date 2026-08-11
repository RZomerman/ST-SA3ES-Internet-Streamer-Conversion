#!/usr/bin/env python3
"""Decode the final Sony IC701 tuning frequency from a CSV or .sr capture.

The capture channels must be ordered CE, DIN, DATA, CLK. Only the final
frequency frame is reported; intermediate tuning frames are discarded.
"""

import argparse
import csv
import re
import sys
import zipfile
from collections.abc import Iterable, Iterator

CE_BIT = 0
DATA_BIT = 2
CLK_BIT = 3
FREQUENCY_MARKER = 0x54
DEFAULT_IF_MHZ = 10.7


def reverse_byte(value: int) -> int:
    value = ((value & 0xF0) >> 4) | ((value & 0x0F) << 4)
    value = ((value & 0xCC) >> 2) | ((value & 0x33) << 2)
    return ((value & 0xAA) >> 1) | ((value & 0x55) << 1)


def frequency_from_frame(frame: list[int], if_mhz: float = DEFAULT_IF_MHZ) -> float | None:
    if len(frame) != 3 or frame[2] != FREQUENCY_MARKER:
        return None
    number = reverse_byte(frame[0]) | (reverse_byte(frame[1]) << 8)
    frequency = number * 0.05 - if_mhz
    if 76.0 <= frequency <= 108.0:
        return frequency
    return None


def csv_samples(path: str) -> Iterator[int]:
    with open(path, newline="") as stream:
        rows = csv.reader(stream)
        header = next(rows, None)
        if header is None:
            return
        for row_number, row in enumerate(rows, 2):
            if len(row) < 4:
                raise ValueError(f"{path}:{row_number}: expected four logic columns")
            try:
                yield sum((int(row[index]) & 1) << index for index in range(4))
            except ValueError as error:
                raise ValueError(f"{path}:{row_number}: non-binary logic value") from error


def sr_samples(path: str) -> Iterator[int]:
    with zipfile.ZipFile(path) as archive:
        names = sorted(
            (name for name in archive.namelist() if name.startswith("logic-1-")),
            key=lambda name: int(name.rsplit("-", 1)[1]),
        )
        for name in names:
            with archive.open(name) as stream:
                while chunk := stream.read(1024 * 1024):
                    yield from chunk


def sample_stream(path: str) -> Iterator[int]:
    if path.lower().endswith(".csv"):
        return csv_samples(path)
    if path.lower().endswith(".sr"):
        return sr_samples(path)
    raise ValueError("input must be a .csv or .sr file")


def decode_final(
    samples: Iterable[int], if_mhz: float = DEFAULT_IF_MHZ
) -> tuple[float | None, list[int], int]:
    previous_ce = 0
    previous_clk = 1
    capturing = False
    bits: list[int] = []
    final_frequency = None
    final_frame: list[int] = []
    frame_count = 0

    for sample in samples:
        ce = (sample >> CE_BIT) & 1
        data = (sample >> DATA_BIT) & 1
        clk = (sample >> CLK_BIT) & 1

        if ce and not previous_ce:
            capturing = True
            bits = []
        elif not ce and previous_ce:
            if len(bits) == 24:
                frame = [sum(bits[index + offset] << (7 - offset)
                             for offset in range(8))
                         for index in range(0, 24, 8)]
                frame_count += 1
                frequency = frequency_from_frame(frame, if_mhz)
                if frequency is not None:
                    final_frequency = frequency
                    final_frame = frame
            capturing = False

        if capturing and previous_clk and not clk:
            bits.append(data)

        previous_ce = ce
        previous_clk = clk

    if capturing and len(bits) == 24:
        frame = [sum(bits[index + offset] << (7 - offset)
                     for offset in range(8))
                 for index in range(0, 24, 8)]
        frame_count += 1
        frequency = frequency_from_frame(frame, if_mhz)
        if frequency is not None:
            final_frequency = frequency
            final_frame = frame

    return final_frequency, final_frame, frame_count


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", help="CSV or sigrok .sr capture")
    parser.add_argument(
        "--if-mhz", type=float, default=DEFAULT_IF_MHZ,
        help=f"IF constant in MHz (default: {DEFAULT_IF_MHZ:.1f})",
    )
    args = parser.parse_args()

    try:
        frequency, frame, frame_count = decode_final(
            sample_stream(args.capture), args.if_mhz
        )
    except (OSError, ValueError, zipfile.BadZipFile, KeyError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2

    if frequency is None:
        print(f"No valid frequency frame found ({frame_count} complete frame(s) decoded).")
        return 1

    print(f"Final frequency: {frequency:.2f} MHz")
    print(f"Frame: {' '.join(f'{byte:02X}' for byte in frame)}")
    print(f"Complete frames: {frame_count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
