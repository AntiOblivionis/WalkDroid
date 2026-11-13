#!/usr/bin/env python3
"""Assemble a legacy Android boot image from caller-supplied inputs.

The default ramdisk starts empty.  Staged files and source manifests are
explicit inputs; use --base-ramdisk image only when intentionally retaining the
ramdisk of the chosen base.  This program never connects to a device.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import stat
import struct

from cpio_newc import pack, parse


MTK_MAGIC = bytes.fromhex('88168858')


def aligned(size, page):
    return (size + page - 1) // page * page


def split_image(raw):
    # The input is the legacy Android boot header used by this project.  Its
    # parser also accepts components with or without MediaTek wrappers.
    kernel_size, _, ramdisk_size, _, second_size, _, _, page, dt_size, _ = \
        struct.unpack_from('<10I', raw, 8)
    offset = page
    parts = []
    for size in (kernel_size, ramdisk_size, second_size, dt_size):
        parts.append(raw[offset:offset + size])
        offset += aligned(size, page)
    return bytearray(raw[:page]), page, parts


def unwrapped(data):
    return data[512:] if data.startswith(MTK_MAGIC) else data


def wrap_like(payload, old):
    if old.startswith(MTK_MAGIC):
        header = bytearray(old[:512])
        struct.pack_into('<I', header, 4, len(payload))
        return bytes(header) + payload
    return payload


def new_header(page, kernel_addr, ramdisk_addr, second_addr, tags_addr, name, cmdline, mtk):
    """Create a format template without reading an existing boot image."""
    header=bytearray(page);header[:8]=b'ANDROID!'
    size=512 if mtk else 0
    struct.pack_into('<10I',header,8,size,kernel_addr,size,ramdisk_addr,0,second_addr,tags_addr,page,0,0)
    label=name.encode('utf-8')[:16];header[48:48+len(label)]=label
    command=cmdline.encode('utf-8')[:1536]
    header[64:64+min(len(command),512)]=command[:512]
    if len(command)>512:header[608:608+len(command[512:])]=command[512:]
    def wrapper(label):
        data=bytearray(b'\xff'*512);data[:40]=bytes(40)
        data[:4]=MTK_MAGIC;data[8:8+len(label)]=label;return bytes(data)
    payloads=[wrapper(b'KERNEL'),wrapper(b'ROOTFS')] if mtk else [b'',b'']
    result=bytearray(header)
    for payload in payloads:
        result.extend(payload);result.extend(bytes(aligned(len(payload),page)-len(payload)))
    return bytes(result)


def add_parent_directories(entries, name, allocate):
    parents = list(PurePosixPath(name).parents)
    for parent in reversed(parents):
        path = str(parent)
        if path == '.' or path in entries:
            continue
        entries[path] = [[allocate(), stat.S_IFDIR | 0o755, 0, 0, 2, 0,
                          0, 0, 0, 0, 0, 0, 0], b'']


def add_entry(entries, name, data, mode, uid, gid, allocate):
    add_parent_directories(entries, name, allocate)
    old = entries.get(name)
    inode = old[0][0] if old else allocate()
    nlink = 2 if stat.S_ISDIR(mode) else 1
    fields = [inode, mode, uid, gid, nlink, 0, len(data), 0, 0, 0, 0, 0, 0]
    entries[name] = [fields, data]


def overlay_tree(entries, root, allocate):
    root = root.resolve()
    for path in sorted(root.rglob('*')):
        name = path.relative_to(root).as_posix()
        mode = path.lstat().st_mode
        if stat.S_ISLNK(mode):
            data = os.fsencode(os.readlink(path))
        elif stat.S_ISDIR(mode):
            data = b''
        elif stat.S_ISREG(mode):
            data = path.read_bytes()
        else:
            # Device nodes in an archive need major/minor metadata, rather
            # than reading a live device on the build host.  Use a manifest.
            raise ValueError('use a manifest for non-file staging entry: ' + name)
        add_entry(entries, name, data, mode, 0, 0, allocate)


def overlay_manifest(entries, manifest, allocate):
    for item in json.loads(manifest.read_text(encoding='utf-8')):
        name = item['path']
        mode = item.get('mode', stat.S_IFREG | 0o644)
        uid, gid = item.get('uid', 0), item.get('gid', 0)
        if stat.S_ISDIR(mode):
            data = b''
        elif stat.S_ISLNK(mode):
            data = os.fsencode(item['target'])
        elif stat.S_ISCHR(mode) or stat.S_ISBLK(mode):
            data = b''
        else:
            data = (manifest.parent / item.get('source', name)).read_bytes()
        add_entry(entries, name, data, mode, uid, gid, allocate)
        entries[name][0][9] = item.get('major', 0)
        entries[name][0][10] = item.get('minor', 0)


def assemble(base, kernel, entries):
    header, page, (old_kernel, old_ramdisk, second, dt) = split_image(base)
    kernel = wrap_like(unwrapped(kernel), old_kernel)
    ramdisk = wrap_like(gzip.compress(pack(entries), mtime=0), old_ramdisk)
    for offset, data in ((8, kernel), (16, ramdisk), (24, second), (40, dt)):
        struct.pack_into('<I', header, offset, len(data))

    # The legacy boot header contains an ID generated from its components.
    # This is generated from the chosen inputs, never used as a build gate.
    digest = hashlib.sha1()
    for data in (kernel, ramdisk, second):
        digest.update(data)
        digest.update(struct.pack('<I', len(data)))
    if dt:
        digest.update(dt)
        digest.update(struct.pack('<I', len(dt)))
    header[576:608] = digest.digest() + bytes(12)
    result = bytearray(header)
    for data in (kernel, ramdisk, second, dt):
        result.extend(data)
        result.extend(bytes(aligned(len(data), page) - len(data)))
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--base-boot', type=Path,
                    help='optional legacy boot image providing header and wrappers; omit to generate a header')
    ap.add_argument('--page-size',type=lambda x:int(x,0),default=2048)
    ap.add_argument('--kernel-address',type=lambda x:int(x,0),default=0)
    ap.add_argument('--ramdisk-address',type=lambda x:int(x,0),default=0)
    ap.add_argument('--second-address',type=lambda x:int(x,0),default=0)
    ap.add_argument('--tags-address',type=lambda x:int(x,0),default=0)
    ap.add_argument('--board-name',default='')
    ap.add_argument('--cmdline',default='')
    ap.add_argument('--mtk-headers',action='store_true',help='Generate MediaTek component wrappers with a new header')
    ap.add_argument('--kernel', type=Path, required=True,
                    help='new zImage, with or without a MediaTek wrapper')
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--base-ramdisk', choices=('empty', 'image'), default='empty')
    ap.add_argument('--staging-tree', type=Path, action='append', default=[],
                    help='tree of caller-provided ramdisk files; may be repeated')
    ap.add_argument('--overlay', type=Path, action='append', default=[],
                    help='JSON entry manifest, files relative to its directory; may be repeated')
    ap.add_argument('--remove', type=Path, action='append', default=[],
                    help='JSON list of ramdisk paths to remove after overlays')
    ap.add_argument('--pad-to', type=int, default=0,
                    help='optional minimum final image length in bytes')
    args = ap.parse_args()

    raw = (args.base_boot.read_bytes() if args.base_boot else
           new_header(args.page_size,args.kernel_address,args.ramdisk_address,args.second_address,
                      args.tags_address,args.board_name,args.cmdline,args.mtk_headers))
    _, _, parts = split_image(raw)
    entries = (parse(gzip.decompress(unwrapped(parts[1])))
               if args.base_ramdisk == 'image' else {})
    next_inode = max((entry[0][0] for entry in entries.values()), default=0)

    def allocate():
        nonlocal next_inode
        next_inode += 1
        return next_inode

    for root in args.staging_tree:
        overlay_tree(entries, root, allocate)
    for manifest in args.overlay:
        overlay_manifest(entries, manifest, allocate)
    for path in args.remove:
        for name in json.loads(path.read_text(encoding='utf-8')):
            entries.pop(name, None)
    output = assemble(raw, args.kernel.read_bytes(), entries)
    if args.pad_to > len(output):
        output.extend(bytes(args.pad_to - len(output)))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print('assembled', args.output, 'bytes', len(output), 'ramdisk entries', len(entries))


if __name__ == '__main__':
    main()
