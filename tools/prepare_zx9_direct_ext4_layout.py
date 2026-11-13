#!/usr/bin/env python3
"""Compose caller-supplied filesystem images into a new local image file.

Each segment supplies an offset and an existing file. Segments are written in
the order given; later segments replace any overlapping bytes. This program does
not select a device or choose filesystem images, sizes, offsets, or identities.
"""
import argparse
import json
from pathlib import Path
import shutil


def number(value):
    return int(value, 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--segment', nargs=2, action='append', default=[],
                        metavar=('OFFSET', 'IMAGE'), help='copy an input file at OFFSET; repeat as needed')
    parser.add_argument('--size-bytes', type=number, default=0,
                        help='initial output length; writes past it extend the file')
    parser.add_argument('--output', type=Path, required=True, help='new local image file')
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    records = []
    with args.output.open('xb') as output:
        output.truncate(args.size_bytes)
        for offset_text, name in args.segment:
            offset = number(offset_text)
            source = Path(name)
            output.seek(offset)
            with source.open('rb') as input_file:
                shutil.copyfileobj(input_file, output, length=1024 * 1024)
            records.append({'offset': offset, 'input': str(source), 'bytes': source.stat().st_size})
    record_path = args.output.with_name(args.output.name + '.layout.json')
    with record_path.open('x', encoding='utf-8') as stream:
        json.dump({'output': str(args.output), 'size_bytes': args.output.stat().st_size,
                   'segments': records}, stream, indent=2)
        stream.write('\n')
    print('Created local image:', args.output)


if __name__ == '__main__':
    main()
