"""Compile the WalkDroid settings APK from the current source tree.

The SDK, aapt2 and signing key can be supplied by the builder.  Each run uses
an empty temporary directory and replaces the requested output on success.
This command does not connect to a device or assemble a system image.
"""
from __future__ import annotations

import argparse
from walkdroid_build_metadata import write_optional, tool_version
import copy
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def run(args: list[object], env: dict[str, str] | None = None) -> None:
    subprocess.run([str(arg) for arg in args], check=True, env=env)


def version(*command: str) -> str:
    return tool_version(command) or "unavailable"


def build() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk-dir', type=Path, default=ROOT / 'tools/android-sdk-28')
    parser.add_argument('--aapt2', default=str(ROOT / 'tools/aapt2_64.exe') if os.name == 'nt' else 'aapt2')
    parser.add_argument('--keystore', type=Path, help='existing PKCS12 signing key; defaults to a locally generated development key')
    parser.add_argument('--key-alias', default='walkdroid-local')
    parser.add_argument('--storepass-env', default='WALKDROID_KEYSTORE_PASSWORD',
                        help='environment variable containing the signing password; defaults to a local development password')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/WalkDroidSonyAudio.apk')
    args = parser.parse_args()

    src = ROOT / 'system/zx300a/sony-settings'
    shared = ROOT / 'system/zx300a/app-exclusive'
    jar, d8 = args.sdk_dir / 'android.jar', args.sdk_dir / 'd8.jar'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    signing_env = os.environ.copy()
    signing_env.setdefault(args.storepass_env, 'walkdroid-local-development')
    if args.keystore is None:
        args.keystore = args.output.parent / 'walkdroid-local-signing.p12'
        if not args.keystore.exists():
            run(['keytool', '-genkeypair', '-keystore', args.keystore, '-storetype', 'PKCS12',
                 '-storepass:env', args.storepass_env, '-keypass:env', args.storepass_env,
                 '-alias', args.key_alias, '-keyalg', 'RSA', '-keysize', '2048', '-validity', '3650',
                 '-dname', 'CN=WalkDroid Local Development, O=Local Development, C=US', '-noprompt'], env=signing_env)
    sources = sorted(src.glob('*.java')) + [shared / name for name in
        ('AppSelectorProtocol.java', 'BtCatalogProtocol.java', 'BtLinkProtocol.java')]

    with tempfile.TemporaryDirectory(prefix='walkdroid-settings-', dir=args.output.parent) as temporary:
        work = Path(temporary)
        classes, dex = work / 'classes', work / 'dex'
        classes.mkdir()
        dex.mkdir()
        run(['javac', '-parameters', '-encoding', 'UTF-8', '--release', '8', '-classpath', jar,
             '-d', classes, *sources])
        run(['java', '-cp', d8, 'com.android.tools.r8.D8', '--min-api', '28', '--lib', jar,
             '--output', dex, *sorted(classes.rglob('*.class'))])
        run([args.aapt2, 'compile', '--dir', src / 'res', '-o', work / 'res.zip'])
        run([args.aapt2, 'link', '-I', jar, '--manifest', src / 'AndroidManifest.xml',
             '-o', work / 'unsigned.apk', work / 'res.zip'])
        with zipfile.ZipFile(work / 'unsigned.apk', 'a', compression=zipfile.ZIP_DEFLATED) as archive:
            archive.write(dex / 'classes.dex', 'classes.dex')
        run(['jarsigner', '-keystore', args.keystore, '-storetype', 'PKCS12', '-storepass:env', args.storepass_env,
             '-sigalg', 'SHA256withRSA', '-digestalg', 'SHA-256', '-signedjar', work / 'signed.apk',
             work / 'unsigned.apk', args.key_alias], env=signing_env)
        aligned = work / 'WalkDroidSonyAudio.apk'
        with zipfile.ZipFile(work / 'signed.apk') as source, zipfile.ZipFile(aligned, 'w') as target:
            for item in source.infolist():
                item = copy.copy(item)
                if item.compress_type == zipfile.ZIP_STORED:
                    pad = (-(target.fp.tell() + 30 + len(item.filename.encode()) + len(item.extra) + 4)) % 4
                    item.extra += struct.pack('<HH', 0xd935, pad) + bytes(pad)
                target.writestr(item, source.read(item.filename))
        shutil.copyfile(aligned, args.output)

    lock = {
        'sources': [str(path.relative_to(ROOT)) for path in sources],
        'resources': str((src / 'res').relative_to(ROOT)),
        'sdk_dir': str(args.sdk_dir), 'aapt2': args.aapt2,
        'java': version('java', '-version'), 'javac': version('javac', '-version'),
        'aapt2_version': version(args.aapt2, 'version'),
        'output': str(args.output),
    }
    write_optional((args.output.parent / 'build.lock.json'), json.dumps(lock, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print(f'Built {args.output}')


if __name__ == '__main__':
    build()
