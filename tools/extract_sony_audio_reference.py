#!/usr/bin/env python3
"""Extract selected vendor directories from a caller-provided local ext image.

Run on Linux or WSL with e2fsprogs. The caller supplies both the image and a
new local output directory. This tool never discovers or connects to a device.
Extracted vendor material may have separate licensing and privacy restrictions.
"""
from __future__ import annotations

import argparse
from walkdroid_build_metadata import write_optional
import json
from pathlib import Path, PurePosixPath
import subprocess


DEFAULT_PATHS = ('/vendor/sony', '/lib', '/etc', '/bin', '/usr/share/audio_dac')


def debugfs_arg(value: str) -> str:
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--image', type=Path, required=True, help='caller-provided local ext filesystem image')
    ap.add_argument('--output', type=Path, required=True, help='new local extraction directory')
    ap.add_argument('--path', action='append', default=[],
                    help='directory inside the image to extract; repeat to replace the default selection')
    ap.add_argument('--debugfs', default='debugfs', help='e2fsprogs debugfs executable')
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    paths = args.path or DEFAULT_PATHS
    commands = []
    for source in paths:
        relative = str(PurePosixPath(source)).lstrip('/')
        parent = args.output / Path(relative).parent
        parent.mkdir(parents=True, exist_ok=True)
        command = 'rdump ' + debugfs_arg(source) + ' ' + debugfs_arg(str(parent))
        result = subprocess.run([args.debugfs, '-R', command, str(args.image)],
                                capture_output=True, text=True, errors='replace')
        commands.append({'path': source, 'destination_parent': str(parent),
                         'returncode': result.returncode,
                         'stdout': result.stdout, 'stderr': result.stderr})
    record = {'image': str(args.image), 'debugfs': args.debugfs, 'commands': commands}
    recorded = write_optional((args.output/'extract.lock.json'),
        json.dumps(record, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    if recorded:
        print('Extraction commands completed; inspect extract.lock.json for diagnostics:', args.output)
    else:
        print(json.dumps(record, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
