#!/usr/bin/env python3
"""Build an optional SystemUI resource overlay for an existing installation.

The builder supplies Android packaging/signing tools and a signing key.
This command does not connect to or change a device.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--android-jar', type=Path, required=True)
    parser.add_argument('--aapt2', type=Path, required=True)
    parser.add_argument('--signapk', type=Path, required=True)
    parser.add_argument('--certificate', type=Path, required=True)
    parser.add_argument('--key', type=Path, required=True)
    parser.add_argument('--java', default='java')
    parser.add_argument('--host-lib-dir', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    resources = ROOT/'system/zx300a/android/device/overlay/frameworks/base/packages/SystemUI/res'
    manifest = ROOT/'system/zx300a/android/systemui-runtime-overlay/AndroidManifest.xml'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    if args.host_lib_dir:
        env['LD_LIBRARY_PATH'] = str(args.host_lib_dir) + os.pathsep + env.get('LD_LIBRARY_PATH', '')
    with tempfile.TemporaryDirectory(prefix='walkdroid-ui-overlay-') as directory:
        work = Path(directory)
        compiled, unsigned, signed = work/'resources.zip', work/'unsigned.apk', work/'signed.apk'
        subprocess.run([str(args.aapt2), 'compile', '--dir', str(resources), '-o', str(compiled)], check=True, env=env)
        subprocess.run([str(args.aapt2), 'link', '-I', str(args.android_jar), '--manifest', str(manifest),
                        '-o', str(unsigned), str(compiled)], check=True, env=env)
        command = [str(args.java)]
        if args.host_lib_dir:
            command.append('-Djava.library.path=' + str(args.host_lib_dir))
        command += ['-jar', str(args.signapk), str(args.certificate), str(args.key), str(unsigned), str(signed)]
        subprocess.run(command, check=True, env=env)
        shutil.copyfile(signed, args.output)
    print('Built optional SystemUI overlay:', args.output)


if __name__ == '__main__':
    main()
