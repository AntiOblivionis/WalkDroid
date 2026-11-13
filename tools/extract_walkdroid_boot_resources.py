#!/usr/bin/env python3
"""Extract selected resources from a caller-provided local Android boot image.

The input is a legacy Android boot header containing a gzip/newc ramdisk,
optionally wrapped in a MediaTek component header. This is an offline tool and
does not select a device or execute any extracted content. Archive symlinks are
resolved inside the archive and copied as ordinary files for portable staging.
"""
from __future__ import annotations

import argparse
from walkdroid_build_metadata import write_optional
import gzip
import json
from pathlib import Path
import posixpath
import stat
import struct

from cpio_newc import parse


def archive_path(name: str) -> str:
    return posixpath.normpath('/' + name).lstrip('/')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--boot', type=Path, required=True, help='caller-provided local boot image')
    ap.add_argument('--output', type=Path, required=True, help='new local extraction directory')
    ap.add_argument('--directory', action='append', default=[],
                    help='ramdisk directory to extract; repeat to replace the default lib directory')
    args = ap.parse_args()
    raw = args.boot.read_bytes()
    kernel_size, _, ramdisk_size, _, _, _, _, page = struct.unpack_from('<8I', raw, 8)
    offset = page + (kernel_size + page - 1) // page * page
    ramdisk = raw[offset:offset + ramdisk_size]
    if ramdisk.startswith(bytes.fromhex('88168858')):
        ramdisk = ramdisk[512:]
    archive = {archive_path(name): value for name, value in parse(gzip.decompress(ramdisk)).items()}
    selected = [archive_path(name).rstrip('/') for name in (args.directory or ['lib'])]
    args.output.mkdir(parents=True, exist_ok=False)
    copied = []
    unresolved = []
    for name, (fields, _) in archive.items():
        if not any(name == directory or name.startswith(directory + '/') for directory in selected):
            continue
        if stat.S_ISDIR(fields[1]):
            (args.output/name).mkdir(parents=True, exist_ok=True)
            continue
        target, visited = name, set()
        while target in archive and target not in visited and stat.S_ISLNK(archive[target][0][1]):
            visited.add(target)
            link = archive[target][1].decode('utf-8', 'replace')
            target = archive_path(link if link.startswith('/') else posixpath.join(posixpath.dirname(target), link))
        if target in archive and stat.S_ISREG(archive[target][0][1]):
            destination = args.output/name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(archive[target][1])
            copied.append({'path': name, 'archive_source': target})
        else:
            unresolved.append(name)
    record = {'boot': str(args.boot), 'directories': selected,
              'copied': copied, 'unresolved': unresolved}
    write_optional((args.output/'extract.lock.json'), 
        json.dumps(record, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print('Extracted', len(copied), 'archive files; unresolved:', len(unresolved))


if __name__ == '__main__':
    main()
