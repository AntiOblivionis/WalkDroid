#!/usr/bin/env python3
"""Back up or write the selected ZX300A music partition from the host.

All offsets are relative to mmcblk0p29. This tool never changes the outer
partition table or writes boot, system firmware, NVP or microSD partitions.
The caller selects the player and supplies its own CID for the device action.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import secrets
import shlex
import subprocess
import tempfile

from extract_walkman_device import adb_command, identity


REGIONS = {
    'music': (0, 4294967296),
    'system': (4294967296, 2147483648),
    'data': (6442450944, 6848774144),
}
CHUNK = 32 * 1024 * 1024


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('backup', 'write'))
    parser.add_argument('--serial', required=True, help='explicit native-mode ADB serial')
    parser.add_argument('--cid', required=True, help='CID read from this same physical player')
    parser.add_argument('--backup', type=Path, help='new host file for a full p29 backup')
    parser.add_argument('--music-image', type=Path, help='new 4 GiB music FAT image, if replacing it')
    parser.add_argument('--system-image', type=Path)
    parser.add_argument('--data-image', type=Path)
    parser.add_argument('--busybox', default='/sbin/busybox', help='BusyBox executable on the player')
    args = parser.parse_args()
    if args.action == 'backup' and not args.backup:
        parser.error('backup requires --backup')
    selected = [(name, image) for name, image in (
        ('music', args.music_image), ('system', args.system_image), ('data', args.data_image)) if image]
    if args.action == 'write' and not selected:
        parser.error('write requires at least one image')

    # Device selection is an operation-level identity confirmation. It is not
    # part of any image, component, build or shared system runtime.
    found = identity(args.serial)
    print('Selected ADB target:', args.serial, 'model:', found['model'],
          'platform:', found['platform'], 'CID:', found['cid'], flush=True)
    if (found['cid'].lower() != args.cid.lower() or
            found['model'] != 'NW-ZX300Series' or found['platform'] != 'mt8590'):
        raise RuntimeError('Selected player identity does not match; no other device will be selected')

    if args.action == 'backup':
        args.backup.parent.mkdir(parents=True, exist_ok=True)
        with args.backup.open('xb') as destination:
            adb_command(args.serial, 'exec-out', 'cat', '/dev/block/mmcblk0p29', stdout=destination)
        print('Read', args.backup.stat().st_size, 'bytes to', args.backup, flush=True)
        print('This is a live partition read, not an atomic snapshot of the whole device.')
        return

    remote = '/dev/shm/walkdroid-' + secrets.token_hex(12)
    busybox = shlex.quote(args.busybox)
    try:
        for name, image in selected:
            base, capacity = REGIONS[name]
            written = 0
            print('Writing', name, 'from', image, 'at byte', base, flush=True)
            with image.open('rb') as source:
                while written < capacity:
                    chunk = source.read(min(CHUNK, capacity - written))
                    if not chunk:
                        break
                    with tempfile.NamedTemporaryFile(prefix='walkdroid-chunk-', delete=False) as file:
                        local = Path(file.name)
                        file.write(chunk)
                    try:
                        adb_command(args.serial, 'push', str(local), remote, stdout=subprocess.DEVNULL)
                        seek = (base + written) // 4096
                        command = (f'{busybox} dd if={shlex.quote(remote)} of=/dev/block/mmcblk0p29 '
                                   f'bs=4096 seek={seek} conv=notrunc')
                        # Some native Sony adbd versions do not propagate the
                        # remote shell exit status. Read an explicit marker.
                        marker = '__WALKDROID_WRITE_STATUS__='
                        wrapped = '{ ' + command + '; }; rc=$?; printf "\\n' + marker + '%s\\n" "$rc"'
                        result = adb_command(args.serial, 'shell', wrapped, capture_output=True, text=True)
                        _, found_marker, status = result.stdout.replace('\r', '').rpartition('\n' + marker)
                        if not found_marker or status.strip() != '0':
                            raise RuntimeError('Write command failed or its outcome is uncertain: ' + result.stderr)
                    finally:
                        local.unlink(missing_ok=True)
                    written += len(chunk)
            print(name, 'bytes written:', written, flush=True)
        adb_command(args.serial, 'shell', busybox + ' sync')
    except BaseException:
        print('Write interrupted; the selected region may be partially written. '
              'Keep the computer backup and do not change devices.', flush=True)
        raise
    finally:
        subprocess.run(['adb', '-s', args.serial, 'shell', busybox + ' rm -f ' + shlex.quote(remote)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    print('Writes submitted. RAM boot is a separate operation.', flush=True)


if __name__ == '__main__':
    main()
