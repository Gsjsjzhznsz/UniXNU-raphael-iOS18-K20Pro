#!/usr/bin/env python3
"""Build the RAPHIOS1 boot-resource container for the one-click recovery loader.

The loader reads a 4 KiB header at 0xC0000000 of the darwin partition, then the
XNUP payload at offset 4096 and an optional "resident" block after it:

    struct '<8sII5Q4I'  magic, version, header_size, total, payload_off,
                        payload_len, resident_off, resident_len,
                        payload_crc, resident_crc, header_crc, flags

The payload is the XNUP container the loader boots: entries of
{ char name[16]; u32 size; u32 reserved } + data padded to 16 bytes.

    python3 tools/raphael/mk-resources.py --kernel kernel.macho --adt kernel.macho.adt \\
        --cmdline "rd=ufsroot0 ufsroot=darwin ..." --out resources.bin

The ADT is embedded as its own entry ("adt") for the loaders that look for it;
the one-click payload itself only carries kernel/ramdisk/cmdline.
"""
import argparse
import hashlib
import struct
import zlib
from pathlib import Path

PAYLOAD_OFF = 4096
BOOT_LINE = 608


def xnup(entries):
    body = b""
    for name, data in entries:
        body += struct.pack("<16sII", name.encode(), len(data), 0)
        body += data + b"\0" * (-len(data) % 16)
    body += struct.pack("<16sII", b"", 0, 0)
    return struct.pack("<4sI", b"XNUP", len(body)) + body


def resources(payload, resident=b"", header_size=4096, flags=1):
    po = header_size
    ro = (po + len(payload) + 4095) // 4096 * 4096
    total = ro + len(resident)
    head = bytearray(header_size)
    struct.pack_into("<8sII5Q4I", head, 0, b"RAPHIOS1", 2, header_size, total,
                     po, len(payload), ro, len(resident),
                     zlib.crc32(payload), zlib.crc32(resident), 0, flags)
    struct.pack_into("<I", head, 64, zlib.crc32(bytes(head)))
    out = bytearray(total)
    out[0:header_size] = head
    out[po:po + len(payload)] = payload
    out[ro:ro + len(resident)] = resident
    return bytes(out), dict(total=total, payload=len(payload), resident=len(resident),
                            ro=ro, crc_ok=True)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--kernel", required=True)
    p.add_argument("--adt", default=None, help="Apple device tree sidecar (entry 'adt')")
    p.add_argument("--ramdisk", default=None, help="optional ramdisk (entry 'ramdisk')")
    p.add_argument("--cmdline", required=True)
    p.add_argument("--resident", default=None, help="optional resident block (NV slots / root image)")
    p.add_argument("--out", required=True)
    a = p.parse_args()

    kernel = Path(a.kernel).read_bytes()
    if kernel[:4] not in (b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf"):
        raise SystemExit("kernel: not a 64-bit Mach-O")
    entries = [("kernel", kernel)]
    if a.ramdisk:
        entries.append(("ramdisk", Path(a.ramdisk).read_bytes()))
    if a.adt:
        entries.append(("adt", Path(a.adt).read_bytes()))
    cmd = a.cmdline.encode()
    if len(cmd) >= BOOT_LINE:
        raise SystemExit(f"cmdline is {len(cmd)} bytes, the loader's buffer is {BOOT_LINE}")
    entries.append(("cmdline", cmd + b"\0" * (BOOT_LINE - len(cmd))))

    payload = xnup(entries)
    resident = Path(a.resident).read_bytes() if a.resident else b""
    data, info = resources(payload, resident)
    Path(a.out).write_bytes(data)
    print(f"{a.out}: {len(data)} bytes, payload {info['payload']} at {PAYLOAD_OFF}, "
          f"resident {info['resident']} at {info['ro']}, sha256 "
          f"{hashlib.sha256(data).hexdigest()}")


if __name__ == "__main__":
    main()
