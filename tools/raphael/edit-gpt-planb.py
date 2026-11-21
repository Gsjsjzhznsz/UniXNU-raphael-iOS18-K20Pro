#!/usr/bin/env python3
"""Plan-B GPT edit for raphael: shrink darwin to 4 GiB and add iosroot after it.

The one-click loader reads RAPHIOS1 at 3 GiB inside the darwin partition, so the
16 GiB iOS root cannot live there any more. This leaves darwin at 4 GiB (room for
the resources) and puts the iOS root in a new "iosroot" partition right after it.

Runs on the host against a phone in TWRP (adb root). Reads the current primary
and backup GPT, rewrites only the four affected LBAs, and verifies the result.

    python3 tools/raphael/edit-gpt-planb.py --apply
    python3 tools/raphael/edit-gpt-planb.py --show
"""
import argparse
import struct
import subprocess
import sys
import uuid

SECTOR = 4096
IOSROOT_TYPE = uuid.UUID("48465300-0000-11aa-aa11-00306543ecac")  # Apple HFS+
ADB = "/vol1/1000/项目/hackios/.tools/platform-tools/adb"
SERIAL = "45c2b4ec"


def adb(*args, binary=False):
    cmd = [ADB, "-s", SERIAL] + list(args)
    out = subprocess.run(cmd, capture_output=True, check=True)
    return out.stdout if binary else out.stdout.decode(errors="replace")


def read_sectors(lba, count):
    out = adb("exec-out", f"dd if=/dev/block/sda bs={SECTOR} skip={lba} count={count} 2>/dev/null", binary=True)
    if len(out) != SECTOR * count:
        raise SystemExit(f"short read at LBA {lba}: {len(out)} != {SECTOR * count}")
    return bytearray(out)


def write_sectors(lba, data):
    # adb exec-in is not reliable in TWRP; write through a temp file on /tmp.
    adb("shell", "rm -f /tmp/gpt.bin")
    p = subprocess.run([ADB, "-s", SERIAL, "shell", "cat > /tmp/gpt.bin"], input=data, capture_output=True, check=True)
    adb("shell", f"dd if=/tmp/gpt.bin of=/dev/block/sda bs={SECTOR} seek={lba} count={len(data)//SECTOR} conv=fsync 2>/dev/null")


def parse(header):
    sig, rev, hsz, crc, res, my, alt, first, last, disk_guid, ent_lba, cnt, esz, ent_crc = \
        struct.unpack_from("<8sIIIIQQQQ16sQIII", header, 0)
    if sig != b"EFI PART":
        raise SystemExit("primary header: bad signature")
    return dict(my=my, alt=alt, first=first, last=last, guid=disk_guid, ent_lba=ent_lba,
                cnt=cnt, esz=esz, ent_crc=ent_crc, hsz=hsz)


def header_crc(header):
    h = bytearray(header)
    struct.pack_into("<I", h, 16, 0)
    return crc32(h[:struct.unpack_from("<I", h, 12)[0]])


def crc32(data):
    import zlib
    return zlib.crc32(data) & 0xFFFFFFFF


def entries_parse(raw, cnt, esz):
    parts = []
    for i in range(cnt):
        e = raw[i * esz:(i + 1) * esz]
        if e[:16] == b"\0" * 16:
            continue
        start, end, attr = struct.unpack_from("<QQQ", e, 32)
        name = e[56:128].decode("utf-16-le", "ignore").rstrip("\0")
        parts.append(dict(index=i, raw=bytearray(e), type=e[:16], name=name, start=start, end=end, attr=attr))
    return parts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--show", action="store_true")
    ap.add_argument("--darwin-gib", type=float, default=4.0)
    a = ap.parse_args()

    prim = read_sectors(1, 1)
    info = parse(prim)
    entries_raw = read_sectors(info["ent_lba"], 1)
    parts = entries_parse(entries_raw, info["cnt"], info["esz"])
    darwin = next((p for p in parts if p["name"] == "darwin"), None)
    if darwin is None:
        raise SystemExit("no darwin partition")

    new_blocks = int(a.darwin_gib * 1024 ** 3) // SECTOR
    new_end = darwin["start"] + new_blocks - 1
    ios_start = new_end + 1
    ios_end = info["last"]
    print(f"darwin {darwin['start']}..{darwin['end']} -> {darwin['start']}..{new_end} ({new_blocks} blocks)")
    print(f"iosroot new: {ios_start}..{ios_end} ({(ios_end - ios_start + 1) * SECTOR / 1024**3:.1f} GiB)")
    if a.show or not a.apply:
        return
    if darwin["end"] != info["last"]:
        raise SystemExit("darwin is not the last partition; layout differs from the plan")
    if ios_start <= new_end or ios_end != info["last"]:
        raise SystemExit("geometry check failed")

    # Rewrite darwin and add iosroot in the primary entries.
    new_parts = [dict(p) for p in parts if p["name"] not in ("darwin", "iosroot")]
    d = dict(darwin)
    struct.pack_into("<QQ", d["raw"], 32, d["start"], new_end)
    d["end"] = new_end
    new_parts.append(d)
    if len(new_parts) > info["cnt"]:
        raise SystemExit("no free GPT slot")
    ios = bytearray(info["esz"])
    ios[0:16] = IOSROOT_TYPE.bytes_le
    ios[16:32] = uuid.uuid4().bytes_le
    struct.pack_into("<QQ", ios, 32, ios_start, ios_end)
    ios[56:56 + 2 * len("iosroot")] = "iosroot".encode("utf-16-le")
    new_parts.append(dict(index=None, raw=ios, name="iosroot", start=ios_start, end=ios_end))

    new_entries = bytearray(info["cnt"] * info["esz"])
    for i, p in enumerate(new_parts):
        new_entries[i * info["esz"]:(i + 1) * info["esz"]] = p["raw"]
    ent_crc = crc32(new_entries)

    def build_header(base, my, alt, ent_lba):
        h = bytearray(base)
        struct.pack_into("<Q", h, 24, my)
        struct.pack_into("<Q", h, 32, alt)
        struct.pack_into("<Q", h, 72, ent_lba)
        struct.pack_into("<I", h, 88, ent_crc)
        struct.pack_into("<I", h, 16, 0)
        struct.pack_into("<I", h, 16, crc32(h[:info["hsz"]]))
        return h

    prim_new = build_header(prim, 1, info["alt"], info["ent_lba"])
    back_ent_lba = info["alt"] - 1
    back_hdr_old = read_sectors(info["alt"], 1)
    back_new = build_header(back_hdr_old, info["alt"], 1, back_ent_lba)

    write_sectors(1, bytes(prim_new))
    write_sectors(info["ent_lba"], bytes(new_entries))
    write_sectors(back_ent_lba, bytes(new_entries))
    write_sectors(info["alt"], bytes(back_new))

    # Verify by reading back.
    prim2 = read_sectors(1, 1)
    info2 = parse(prim2)
    ents2 = read_sectors(info2["ent_lba"], 1)
    parts2 = entries_parse(ents2, info2["cnt"], info2["esz"])
    names = {p["name"]: p for p in parts2}
    if "iosroot" not in names or names["darwin"]["end"] != new_end:
        raise SystemExit("read-back verification failed")
    print("GPT updated and verified:", ", ".join(f"{p['name']}={p['start']}..{p['end']}" for p in parts2))


if __name__ == "__main__":
    main()
