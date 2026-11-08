#!/usr/bin/env python3
"""Download GPS SPI logs over USART2, or decode a saved raw dump to CSV."""
import argparse
import csv
from pathlib import Path
import struct
import sys
import time
import zlib

BANNER = b"\r\nGPS LOG DUMP 1\r\n"
SECTOR = 4096
CAPACITY = 32 * 1024 * 1024 - SECTOR
MAGIC = 0x31535047
PAGE_SIZE = 256
HEADER = struct.Struct("<7I")
RECORD = struct.Struct("<QIiiiiiiIIBBBBII")
FIELDS = ["unix_time_ms", "i_tow_ms", "latitude_deg", "longitude_deg",
          "altitude_msl_m", "velocity_north_m_s", "velocity_east_m_s",
          "velocity_down_m_s", "horizontal_accuracy_m", "vertical_accuracy_m",
          "fix_type", "satellites_used", "fix_flags", "time_valid_flags",
          "sequence", "crc32"]


def read_exact(port, length):
    data = bytearray()
    while len(data) < length:
        part = port.read(length - len(data))
        if not part:
            raise RuntimeError("Serial timeout; incomplete dump retained on disk")
        data.extend(part)
    return data


def receive_dump(port, output, timeout=10):
    """Request the GPS dump, save raw sectors, and return the sector count."""
    port.reset_input_buffer()
    port.write(b"LOG_DUMP\n")
    prefix = bytearray()
    deadline = time.monotonic() + timeout
    while not prefix.endswith(BANNER):
        if time.monotonic() >= deadline:
            raise RuntimeError("Timed out waiting for GPS LOG DUMP response")
        prefix.extend(read_exact(port, 1))
        if b"REFUSED\r\n" in prefix:
            raise RuntimeError("LOG_DUMP refused. Send LOG_END, then wait "
                               "for LOG_STATUS to report LOG COMPLETE")
        if len(prefix) > 4096:
            raise RuntimeError("Unexpected serial output before dump banner")
    sectors = 0
    while True:
        tag = read_exact(port, 1)
        if tag == b"E":
            return sectors
        if tag == b"X":
            raise RuntimeError("Board reported flash read failure; partial dump retained")
        if tag != b"D" or (sectors + 1) * SECTOR > CAPACITY:
            raise RuntimeError("Invalid dump framing/size; partial dump retained")
        output.write(read_exact(port, SECTOR))
        sectors += 1


def download(port_name, output, baud=115200, timeout=10):
    try:
        import serial
    except ImportError as exc:
        raise RuntimeError("Install pyserial: python -m pip install pyserial") from exc
    # Reserve the filename before requesting any bytes. Never replace a prior log.
    with Path(output).open("xb") as target:
        with serial.Serial(port=None, baudrate=baud, timeout=timeout,
                           write_timeout=timeout) as port:
            port.dtr = False
            port.rts = False
            port.port = port_name
            port.open()
            sectors = receive_dump(port, target, timeout)
            target.flush()
    print(f"Saved {sectors} sectors ({sectors * SECTOR} bytes) to {output}. "
          "Run parse to verify the log.", file=sys.stderr)


def csv_values(data):
    values = list(RECORD.unpack(data))
    values[2] /= 1e7
    values[3] /= 1e7
    for field in range(4, 10):
        values[field] /= 1000
    values[-1] = f"0x{values[-1]:08X}"
    return values


def parse_dump(source, output):
    """Verify a GPS1 session; write CSV and return (records, dropped).

    Like the flight parser, raise ValueError on corruption or a missing footer.
    Any valid prefix already written is preserved. Use decode() for recovery.
    """
    writer = csv.writer(output)
    writer.writerow(FIELDS)
    records = expected_sequence = previous_dropped = 0
    while page := source.read(PAGE_SIZE):
        address = SECTOR + expected_sequence * PAGE_SIZE
        if expected_sequence * PAGE_SIZE >= CAPACITY:
            raise ValueError("Dump exceeds GPS flash logging region")
        if len(page) != PAGE_SIZE:
            raise ValueError(f"truncated page at 0x{address:x}")
        if page == b"\xff" * PAGE_SIZE:
            break
        magic, kind, version, sequence, count, total, dropped = HEADER.unpack_from(page)
        if magic != MAGIC or version != 1 or sequence != expected_sequence:
            raise ValueError(f"invalid GPS1 header/version/sequence at 0x{address:x}")
        if kind not in (1, 2, 3) or (expected_sequence == 0) != (kind == 1):
            raise ValueError(f"unexpected page type at 0x{address:x}")
        if (kind == 2 and not 1 <= count <= 4) or (kind != 2 and count != 0):
            raise ValueError(f"invalid record count at 0x{address:x}")
        expected_crc, = struct.unpack_from("<I", page, 252)
        if zlib.crc32(page[:252]) != expected_crc:
            raise ValueError(f"page CRC mismatch at 0x{address:x}")
        if total != records or dropped < previous_dropped:
            raise ValueError(f"record/drop totals do not match at 0x{address:x}")
        previous_dropped = dropped
        if kind == 3:
            # The final physical sector includes unused erased pages.
            remaining = CAPACITY - (expected_sequence + 1) * PAGE_SIZE
            while trailing := source.read(SECTOR):
                if len(trailing) > remaining:
                    raise ValueError("Dump exceeds GPS flash logging region")
                remaining -= len(trailing)
                if any(byte != 0xFF for byte in trailing):
                    raise ValueError("unexpected data after completion footer")
            return records, dropped
        if kind == 2:
            rows = []
            for index in range(count):
                start = HEADER.size + index * RECORD.size
                data = page[start:start + RECORD.size]
                expected_crc, = struct.unpack_from("<I", data, 52)
                if zlib.crc32(data[:52]) != expected_crc:
                    raise ValueError(f"record CRC mismatch at 0x{address + start:x}")
                rows.append(csv_values(data))
            writer.writerows(rows)
            records += count
        expected_sequence += 1
    raise ValueError(f"no valid completion footer; recovered {records} records. "
                     "Use parse --recover for an interrupted or full log")


def decode(raw, target):
    """Write CRC-verified records; return (records, warnings, clean_footer)."""
    if len(raw) > CAPACITY:
        raise ValueError("Dump exceeds GPS flash logging region")
    writer = csv.writer(target)
    writer.writerow(FIELDS)
    records = warnings = 0
    expected = 0
    header = footer = False
    for offset in range(0, len(raw), 256):
        page = raw[offset:offset + 256]
        address = SECTOR + offset
        if page == b"\xff" * 256:
            continue
        if len(page) != 256:
            print(f"Warning: truncated page at 0x{address:x}", file=sys.stderr)
            warnings += 1
            continue
        magic, kind, version, seq, count, total, dropped = struct.unpack_from("<7I", page)
        valid = (magic == MAGIC and version == 1 and kind in (1, 2, 3)
                 and ((kind == 2 and 1 <= count <= 4) or
                      (kind != 2 and count == 0))
                 and zlib.crc32(page[:252]) == struct.unpack_from("<I", page, 252)[0])
        if not valid:
            print(f"Warning: corrupt/torn page at 0x{address:x}; skipped", file=sys.stderr)
            warnings += 1
            continue
        if seq != expected:
            print(f"Warning: page sequence {seq}, expected {expected}", file=sys.stderr)
            warnings += 1
        expected = seq + 1
        if kind == 1:
            header = True
        elif kind == 3:
            footer = True
            if total != records or dropped:
                print(f"Footer: stored={total}, decoded={records}, dropped={dropped}",
                      file=sys.stderr)
                warnings += 1
        else:
            for index in range(count):
                data = page[28 + index * 56:28 + (index + 1) * 56]
                if zlib.crc32(data[:52]) != struct.unpack_from("<I", data, 52)[0]:
                    print(f"Warning: record CRC failed at 0x{address + 28 + index * 56:x}",
                          file=sys.stderr)
                    warnings += 1
                    continue
                writer.writerow(csv_values(data))
                records += 1
    if not header and raw:
        print("Warning: no valid session header", file=sys.stderr)
        warnings += 1
    if not footer and raw:
        print("Warning: no footer; session interrupted, full, or not ended cleanly", file=sys.stderr)
        warnings += 1
    return records, warnings, footer


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    fetch = commands.add_parser("download", help="Save a raw GPS serial dump")
    fetch.add_argument("port", help="Serial port, e.g. COM7")
    fetch.add_argument("output", type=Path, help="New .bin output file")
    fetch.add_argument("--baud", type=int, default=115200)
    fetch.add_argument("--timeout", type=float, default=10)
    verify = commands.add_parser("parse", help="Verify a GPS1 session and write CSV")
    verify.add_argument("input", type=Path)
    verify.add_argument("output", type=Path, help="New .csv output file")
    verify.add_argument("--recover", action="store_true",
                        help="Recover CRC-valid pages despite damage/missing footer")
    dump = commands.add_parser("dump", help="Save raw sectors; never erases flash")
    dump.add_argument("--port", required=True, help="COM port, e.g. COM7")
    dump.add_argument("--output", type=Path, required=True)
    dump.add_argument("--baud", type=int, default=115200)
    dump.add_argument("--timeout", type=float, default=10)
    parse = commands.add_parser("decode", help="Convert a dump to CSV, checking CRCs")
    parse.add_argument("input", type=Path)
    parse.add_argument("--csv", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        if args.command in ("download", "dump"):
            if args.baud <= 0 or args.timeout <= 0:
                raise ValueError("Baud and timeout must be positive")
            download(args.port, args.output, args.baud, args.timeout)
        elif args.command == "parse" and not args.recover:
            with args.input.open("rb") as source, args.output.open(
                    "x", newline="", encoding="utf-8") as target:
                records, dropped = parse_dump(source, target)
            print(f"Verified {records} GPS records. Firmware reports {dropped} dropped.")
        else:
            raw = args.input.read_bytes()
            output = args.csv if args.command == "decode" else args.output
            with output.open("x", newline="", encoding="utf-8") as target:
                records, warnings, _ = decode(raw, target)
            print(f"Decoded {records} records; {warnings} warnings", file=sys.stderr)
    except (OSError, RuntimeError, ValueError) as exc:
        parser.exit(1, f"Error: {exc}. Any output already written was preserved.\n")


if __name__ == "__main__":
    main()
