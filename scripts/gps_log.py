#!/usr/bin/env python3
"""Download GPS SPI logs over USART2, or decode a saved raw dump to CSV."""
import argparse
import csv
from pathlib import Path
import struct
import sys
import zlib

BANNER = b"\r\nGPS LOG DUMP 1\r\n"
SECTOR = 4096
CAPACITY = 32 * 1024 * 1024 - SECTOR
MAGIC = 0x31535047
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
            port.reset_input_buffer()
            port.write(b"LOG_DUMP\n")
            prefix = bytearray()
            while not prefix.endswith(BANNER):
                prefix.extend(read_exact(port, 1))
                if b"REFUSED\r\n" in prefix:
                    raise RuntimeError("LOG_DUMP refused. Send LOG_END, then wait "
                                       "for LOG_STATUS to report LOG COMPLETE")
                if len(prefix) > 4096:
                    raise RuntimeError("Unexpected serial output before dump banner")
            total = 0
            while True:
                tag = read_exact(port, 1)
                if tag == b"E":
                    break
                if tag == b"X":
                    raise RuntimeError("Board reported flash read failure; partial dump retained")
                if tag != b"D" or total + SECTOR > CAPACITY:
                    raise RuntimeError("Invalid dump framing/size; partial dump retained")
                target.write(read_exact(port, SECTOR))
                total += SECTOR
            target.flush()
    print(f"Saved {total} bytes to {output}", file=sys.stderr)


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
                 and count <= 4 and (kind == 2 or count == 0)
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
                values = list(RECORD.unpack(data))
                values[2] /= 1e7
                values[3] /= 1e7
                for field in range(4, 10):
                    values[field] /= 1000
                values[-1] = f"0x{values[-1]:08X}"
                writer.writerow(values)
                records += 1
    if not header and raw:
        print("Warning: no valid session header", file=sys.stderr)
        warnings += 1
    if not footer and raw:
        print("Warning: no footer; session interrupted, full, or not ended cleanly", file=sys.stderr)
        warnings += 1
    return records, warnings, footer


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    dump = commands.add_parser("dump", help="Save raw sectors; never erases flash")
    dump.add_argument("--port", required=True, help="COM port, e.g. COM7")
    dump.add_argument("--output", type=Path, required=True)
    dump.add_argument("--baud", type=int, default=115200)
    dump.add_argument("--timeout", type=float, default=10)
    parse = commands.add_parser("decode", help="Convert a dump to CSV, checking CRCs")
    parse.add_argument("input", type=Path)
    parse.add_argument("--csv", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "dump":
            download(args.port, args.output, args.baud, args.timeout)
        else:
            with args.csv.open("x", newline="", encoding="utf-8") as target:
                records, warnings, _ = decode(args.input.read_bytes(), target)
            print(f"Decoded {records} records; {warnings} warnings", file=sys.stderr)
    except (OSError, RuntimeError, ValueError) as exc:
        parser.exit(1, f"Error: {exc}\n")


if __name__ == "__main__":
    main()
