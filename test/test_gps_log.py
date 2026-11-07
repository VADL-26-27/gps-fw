"""Cross-check the actual C logger dump with Python/zlib and the CSV decoder."""
import contextlib
import importlib.util
import io
from pathlib import Path
import struct
import sys
import tempfile
import types
import zlib

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("gps_log", "scripts/gps_log.py")
gps_log = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gps_log)

raw = Path("build/test_gps_dump.bin").read_bytes()
target = io.StringIO()
with contextlib.redirect_stderr(io.StringIO()):
    records, warnings, complete = gps_log.decode(raw, target)
assert records == 9 and warnings == 1 and complete  # One deliberate queue overflow.
rows = target.getvalue().splitlines()
assert len(rows) == 10
assert "36.1434131,-86.8053556,185.713,0.148,-0.069,-0.17" in rows[1]
assert rows[1].split(",")[-2] == "1"
assert rows[-1].split(",")[-2] == "9"
for offset in range(0, 5 * 256, 256):
    page = raw[offset:offset + 256]
    assert zlib.crc32(page[:252]) == struct.unpack_from("<I", page, 252)[0]

damaged = bytearray(raw)
damaged[256 + 40] ^= 0x80
with contextlib.redirect_stderr(io.StringIO()):
    records, warnings, complete = gps_log.decode(damaged, io.StringIO())
assert records == 5 and warnings >= 2 and complete
with contextlib.redirect_stderr(io.StringIO()):
    records, warnings, complete = gps_log.decode(raw[:4 * 256], io.StringIO())
assert records == 9 and warnings >= 1 and not complete

# A valid page CRC must not conceal an invalid per-record CRC.
damaged = bytearray(raw)
damaged[256 + 40] ^= 0x80
struct.pack_into("<I", damaged, 256 + 252, zlib.crc32(damaged[256:256 + 252]))
with contextlib.redirect_stderr(io.StringIO()):
    records, warnings, complete = gps_log.decode(damaged, io.StringIO())
assert records == 8 and warnings >= 2 and complete

class FakeSerial:
    stream = b""
    writes = 0

    def __init__(self, **kwargs):
        self.data = bytearray(self.stream)
    def __enter__(self):
        return self
    def __exit__(self, *args):
        pass
    def open(self):
        pass
    def reset_input_buffer(self):
        pass
    def write(self, data):
        assert data == b"LOG_DUMP\n"
        FakeSerial.writes += 1
    def read(self, length):
        part = self.data[:min(length, 7)]  # Deliberately fragmented serial reads.
        del self.data[:len(part)]
        return part

sys.modules["serial"] = types.SimpleNamespace(Serial=FakeSerial)
with tempfile.TemporaryDirectory(dir="build") as directory:
    destination = Path(directory) / "log.bin"
    FakeSerial.stream = gps_log.BANNER + b"D" + raw + b"E"
    with contextlib.redirect_stderr(io.StringIO()):
        gps_log.download("COM_TEST", destination)
    assert destination.read_bytes() == raw
    try:
        gps_log.download("COM_TEST", destination)
        raise AssertionError("Existing dump overwritten")
    except FileExistsError:
        pass
    assert FakeSerial.writes == 1
    for index, stream in enumerate((b"LOG_DUMP REFUSED\r\n",
                                    gps_log.BANNER + b"D" + raw + b"X",
                                    gps_log.BANNER + b"D" + raw[:100])):
        FakeSerial.stream = stream
        partial = Path(directory) / f"partial-{index}.bin"
        try:
            gps_log.download("COM_TEST", partial)
            raise AssertionError("Failed transfer accepted")
        except RuntimeError:
            pass
        assert partial.exists()
        if index == 1:
            assert partial.read_bytes() == raw
print("GPS dump decoder tests passed: C wire format, signed fields, page/record "
      "CRC-32, CSV values, corrupt page recovery, interrupted session, serial "
      "download framing/timeouts, partial preservation, overwrite refusal.")
