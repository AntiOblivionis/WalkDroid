#!/usr/bin/env python3
"""Read private Walkman One inputs from one explicitly selected player.

Run inspect first. Supply the CID reported for that same physical player to
extract. No information is transmitted elsewhere; no partition is written.
The resulting directory contains proprietary material and must remain private.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys


def adb_command(serial: str, *args: str, **kwargs):
    return subprocess.run(['adb', '-s', serial, *args], check=True, **kwargs)


def identity(serial: str):
    command = ('getprop ro.serialno; getprop ro.model.number; '
               'getprop ro.board.platform; cat /sys/block/mmcblk0/device/cid')
    result = adb_command(serial, 'shell', command, capture_output=True, text=True, timeout=20)
    values = result.stdout.replace('\r', '').splitlines()
    if len(values) != 4:
        raise RuntimeError('Could not read the selected player identity; reconnect and inspect again')
    return dict(zip(('property_serial', 'model', 'platform', 'cid'), values))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('inspect', 'extract'))
    parser.add_argument('--serial', required=True, help='explicit ADB serial of the selected player')
    parser.add_argument('--cid', help='eMMC CID read from this player during inspect')
    parser.add_argument('--output', type=Path, help='new private directory for extract')
    args = parser.parse_args()
    found = identity(args.serial)
    print('ADB target:', args.serial)
    for key, value in found.items():
        print(key + ':', value)
    if args.action == 'inspect':
        return
    if not args.cid or not args.output:
        parser.error('extract requires --cid and --output')
    if (found['cid'].lower() != args.cid.lower() or
            found['model'] != 'NW-ZX300Series' or found['platform'] != 'mt8590'):
        raise RuntimeError('Selected player identity does not match; no other device will be selected')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    vendor = output / 'vendor-system'
    (vendor / 'vendor').mkdir(parents=True)
    (vendor / 'usr/share').mkdir(parents=True)
    for source, parent in (
        ('/system/vendor/sony', vendor / 'vendor'),
        ('/system/lib', vendor), ('/system/etc', vendor), ('/system/bin', vendor),
        ('/system/usr/share/audio_dac', vendor / 'usr/share'),
        ('/system/usr/share/alsa', vendor / 'usr/share'),
    ):
        print('Reading', source, flush=True)
        adb_command(args.serial, 'pull', source, str(parent))
    print('Reading original boot partition to the computer', flush=True)
    with (output / 'stock-boot.img').open('xb') as destination:
        adb_command(args.serial, 'exec-out', 'cat', '/dev/block/mmcblk0p8', stdout=destination)
    subprocess.run([sys.executable, str(Path(__file__).with_name('extract_walkdroid_boot_resources.py')),
                    '--boot', str(output / 'stock-boot.img'),
                    '--output', str(output / 'vendor-boot')], check=True)
    print('Private files saved to', output)
    print('Do not publish this directory or use stock-boot.img for Fastboot RAM boot.')


if __name__ == '__main__':
    main()
