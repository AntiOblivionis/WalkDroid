#!/usr/bin/env python3
"""Expand a caller-provided length-prefixed zlib firmware stream locally.

This operates on one supplied file and writes one new output file. It does not
look up a historical archive, compare fixed hashes, or connect to a device.
"""
from __future__ import annotations

import argparse
from walkdroid_build_metadata import write_optional
import json
from pathlib import Path
import struct
import zlib


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--input', type=Path, required=True, help='local length-prefixed zlib stream')
    ap.add_argument('--output', type=Path, required=True, help='new expanded local file')
    args = ap.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    chunks = written = 0
    with args.input.open('rb') as source, args.output.open('xb') as destination:
        while True:
            length_bytes = source.read(4)
            if not length_bytes:
                break
            length = struct.unpack('<I', length_bytes)[0]
            if not length:
                break
            data = zlib.decompress(source.read(length))
            destination.write(data)
            written += len(data)
            chunks += 1
    record = {'input': str(args.input), 'output': str(args.output),
              'chunks': chunks, 'output_bytes': written}
    write_optional(Path(str(args.output)+'.expand.lock.json'),
        json.dumps(record, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print('Expanded', chunks, 'chunks to', written, 'bytes:', args.output)


if __name__ == '__main__':
    main()
