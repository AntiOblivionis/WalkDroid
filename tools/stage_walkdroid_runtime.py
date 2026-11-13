#!/usr/bin/env python3
"""Assemble local data/runtime directories from fresh builds and supplied Sony assets.

This is an offline staging tool. Sony libraries, firmware and any optional
configuration remain the caller's inputs; the result is not a source release.
No device operations are performed, and Bluetooth data is not automatically
seeded. The caller-provided files are not inspected for personal data.
"""
from __future__ import annotations
import argparse
from walkdroid_build_metadata import write_optional
import json
from pathlib import Path
import shutil
import struct
import tarfile
import zlib


def property_area(chip: str) -> bytes:
    # Layout consumed by the Sony glibc property bridge; values are inputs.
    values = {'persist.mtk.wcn.combo.chipid': chip, 'service.nvram_init': 'Ready'}
    data = bytearray(4096)
    struct.pack_into('<4I', data, 0, len(values), 0, 0x504f5250, 0x45434f76)
    for i, (name, value) in enumerate(values.items()):
        key, val = name.encode('ascii'), value.encode('ascii')
        off = 128+i*128
        struct.pack_into('<I', data, 32+i*4, (len(key)<<24)|off)
        struct.pack_into('<32sI92s', data, off, key, len(val)<<24, val)
    return bytes(data)


def synthetic_snapshot() -> bytes:
    # Neutral fixture for a new data directory. It is not an extracted NVP and
    # has not been accepted against a Sony DSP; callers may supply their own.
    # Offset 17280 is the Walkman volume; 60/120 avoids a silent fresh install.
    data = bytearray(28672)
    for offset, value in ((4100, 0x596f0ddf), (16396, 2), (16404, 1), (16408, 1),
                          (16436, 1), (16596, 2), (16600, 2), (16604, 2),
                          (16608, 2), (17280, 60), (17308, 2)):
        struct.pack_into('<I', data, offset, value)
    checksum_data = bytearray(data); checksum_data[4096:4216] = bytes(120)
    struct.pack_into('<I', data, 4096, zlib.crc32(checksum_data))
    return bytes(data)


def write_runtime_tar(stage: Path, destination: Path, root_name: str = 'data'):
    executables = {'sony-output-worker', 'sony-output-ai-worker', 'sony-output-ai-guarded-worker',
                   'sony-receiver-worker', 'sony-pcm-sink-worker', 'sony-receiver-legacy-worker',
                   'sony-ai-worker', 'sony-dsf-worker', 'sony-dsd-sink-worker', 'sony-bt-live-source',
                   'sony-bt-tx-worker', 'sony-bt-tx-supervisor', 'sony-bt-receiverd', 'sony-bt-live-check', 'mtkbt'}
    def metadata(member):
        member.uid = member.gid = 0; member.uname = member.gname = ''
        name = member.name.rsplit('/',1)[-1]
        member.mode = 0o755 if member.isdir() or name in executables or name.startswith('ld-') else 0o644
        return member
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(destination, 'w') as archive:
        archive.add(stage, arcname=root_name, filter=metadata)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--native-output', type=Path, required=True)
    ap.add_argument('--sony-output', type=Path, required=True)
    ap.add_argument('--sony-libs', type=Path, action='append', required=True,
                    help='caller-provided Sony/ARM glibc library directory; repeat as needed')
    ap.add_argument('--sony-lib-override', action='append', nargs=2, metavar=('NAME', 'FILE'), default=[],
                    help='select a caller-provided library file or alias after directory search; repeat as needed')
    ap.add_argument('--sony-ai-libs', type=Path, action='append', default=[],
                    help='caller-provided library directory overlaid only in the AI root; repeat as needed')
    ap.add_argument('--sony-assets', type=Path, help='caller-provided rootfs with Sony profiles and audio resources')
    ap.add_argument('--asset', action='append', nargs=2, metavar=('DEST', 'SOURCE'), default=[],
                    help='copy a caller-provided file or directory to a rootfs-relative destination; repeat as needed')
    ap.add_argument('--sony-ai-asset', action='append', nargs=2, metavar=('DEST', 'SOURCE'), default=[],
                    help='copy a caller-provided file or directory only into the AI root; repeat as needed')
    ap.add_argument('--mtkbt', type=Path, help='caller-provided original Bluetooth daemon')
    ap.add_argument('--initial-snapshot', type=Path, help='optional caller-provided audio configuration')
    ap.add_argument('--synthetic-snapshot', action='store_true', help='stage an experimental neutral audio configuration')
    ap.add_argument('--bt-chip', default='0x8590', help='property value for the Sony Bluetooth runtime')
    ap.add_argument('--output', type=Path, required=True, help='new local staging directory')
    ap.add_argument('--tar', type=Path, help='optional tar of the staged data tree')
    ap.add_argument('--tar-root', default='data', help='archive root name; use . for a data-filesystem root')
    a = ap.parse_args()
    a.output.mkdir(parents=True, exist_ok=False)
    stage = a.output/'data'; walk = stage/'walkdroid'
    stage.mkdir()
    def output_files(folder: Path):
        return sorted((p for p in folder.iterdir() if p.is_file() and p.name != 'build.lock.json'),
                      key=lambda p:p.name)
    def shared_library(name: str) -> bool:
        return name.endswith('.so') or '.so.' in name
    sony_files = output_files(a.sony_output)
    native_files = output_files(a.native_output)
    sony_libs = {}
    for folder in a.sony_libs:
        for p in folder.iterdir():
            if p.is_file() and shared_library(p.name):
                sony_libs[p.name] = p
    for name, source in a.sony_lib_override:
        sony_libs[Path(name).name] = Path(source)
    sony_ai_libs = {}
    for folder in a.sony_ai_libs:
        for p in folder.iterdir():
            if p.is_file() and shared_library(p.name):
                sony_ai_libs[p.name] = p
    built_sony_libs = [p for p in sony_files if shared_library(p.name)]
    built_native_libs = [p for p in native_files if shared_library(p.name)]

    def copy(source: Path, target: Path, executable: bool = False):
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        target.chmod(0o755 if executable else 0o644)

    asset_paths = ('etc/asound.conf', 'usr/share/alsa', 'usr/share/audio_dac',
                   'system/usr/share/audio_dac', 'system/vendor/sony/etc/effector_params',
                   'system/vendor/sony/etc/audioanalyzer_params', 'opt/walkdroid/profiles')
    def copy_assets(root: Path, mappings):
        for dest, source_name in mappings:
            source = Path(source_name)
            if source.is_dir():
                shutil.copytree(source, root/dest, dirs_exist_ok=True)
            else:
                copy(source, root/dest)

    def assets(root: Path, *, ai: bool = False):
        if a.sony_assets:
            for name in asset_paths:
                source = a.sony_assets/name
                if source.is_dir():
                    shutil.copytree(source, root/name, dirs_exist_ok=True)
                elif source.is_file():
                    copy(source, root/name)
        copy_assets(root, a.asset)
        if ai:
            copy_assets(root, a.sony_ai_asset)

    def rootfs(root: Path, executables: dict[str, str], *, radio: bool = False, ai: bool = False):
        for folder in ('bin', 'lib', 'tmp', 'dev', 'dev/shm', 'data', 'data/config', 'etc'):
            (root/folder).mkdir(parents=True, exist_ok=True)
        if radio:
            (root/'empty-nvram').mkdir(exist_ok=True)
            (root/'data/nvram').mkdir(exist_ok=True)
            (root/'etc/sony-bt-properties').write_bytes(property_area(a.bt_chip))
            nodes = ('null', 'urandom', 'stpbt')
            if a.mtkbt:
                copy(a.mtkbt, root/'bin/mtkbt', True)
        else:
            (root/'dev/snd').mkdir(exist_ok=True)
            nodes = ('null', 'urandom', 'snd/controlC0', 'snd/pcmC0D0p',
                     'snd/pcmC0D2p', 'snd/pcmC0D4p', 'snd/timer')
        for node in nodes:
            (root/'dev'/node).touch()
        assets(root, ai=ai)
        for name, source in sony_libs.items():
            copy(source, root/'lib'/name, executable=name.startswith('ld-'))
        if ai:
            for name, source in sony_ai_libs.items():
                copy(source, root/'lib'/name, executable=name.startswith('ld-'))
        for source in built_sony_libs:
            copy(source, root/'lib'/source.name)
        for dest, source in executables.items():
            copy(a.sony_output/source, root/'bin'/dest, True)

    playback = {'sony-output-worker':'sony-output-normal-worker',
                'sony-output-ai-worker':'sony-output-ai-worker',
                'sony-output-ai-guarded-worker':'sony-output-ai-parent-worker'}
    receiver = {'sony-receiver-worker':'sony-receiver-worker'}
    sink = {'sony-pcm-sink-worker':'sony-pcm-sink-worker',
            'sony-receiver-legacy-worker':'sony-receiver-legacy-worker'}
    rootfs(walk/'runtime/playback/rootfs', playback)
    rootfs(walk/'runtime/ai/rootfs', {'sony-ai-worker':'sony-ai-worker'}, ai=True)
    rootfs(walk/'receiver/receiver-root', receiver)
    rootfs(walk/'receiver/sink-root', sink)
    rootfs(walk/'player/dsf-root', {'sony-dsf-worker':'sony-dsf-worker'})
    rootfs(walk/'player/sink-root', sink)
    rootfs(walk/'player/native-root', {'sony-dsd-sink-worker':'sony-dsd-sink-worker'})
    rootfs(walk/'bt-receiver/receiver-root', receiver)
    rootfs(walk/'bt-receiver/sink-root', sink)
    rootfs(walk/'bt-receiver/radio-root', {'sony-bt-live-source':'sony-bt-live-source'}, radio=True)
    rootfs(walk/'bt-output/radio-root', {'sony-bt-tx-worker':'sony-bt-tx-worker'}, radio=True)
    for group in ('bt-receiver', 'bt-output'):
        for source in built_native_libs:
            copy(source, walk/group/'lib'/source.name)
    for name in ('sony-bt-receiverd', 'sony-bt-live-check'):
        copy(a.native_output/name, walk/'bt-receiver'/name, True)
    copy(a.native_output/'sony-bt-tx-supervisor', walk/'bt-output/sony-bt-tx-supervisor', True)
    # The original Bluetooth daemon creates its databases under these paths;
    # starting with empty directories requires no device-specific pair data.
    for name in ('nvram', '@btmtk', 'Bluetooth/devdb'):
        (walk/'bt-receiver/peer-template'/name).mkdir(parents=True, exist_ok=True)
    for name in ('sony-receiver/sessions', 'sony-dsf-player/sessions',
                 'sony-bt-receiver/b99-sessions', 'sony-audio-controls-v11'):
        (stage/name).mkdir(parents=True, exist_ok=True)
    if a.initial_snapshot:
        copy(a.initial_snapshot, stage/'sony-audio-controls-v11/current.bin')
    elif a.synthetic_snapshot:
        (stage/'sony-audio-controls-v11/current.bin').write_bytes(synthetic_snapshot())
    info = {'kind':'local-runtime-staging', 'sony_components':len(sony_files),
            'native_components':len(native_files), 'mtkbt_supplied':bool(a.mtkbt),
            'sony_assets_supplied':bool(a.sony_assets), 'initial_snapshot_supplied':bool(a.initial_snapshot),
            'additional_asset_mappings':len(a.asset),
            'sony_library_overrides':{name:str(source) for name,source in a.sony_lib_override},
            'synthetic_snapshot':bool(a.synthetic_snapshot and not a.initial_snapshot),
            'automatic_bluetooth_seed_added':False,
            'inputs':vars(a)}
    write_optional((a.output/'stage.json'), json.dumps(info, ensure_ascii=False, indent=2, default=str)+'\n', encoding='utf-8')
    if a.tar:
        write_runtime_tar(stage, a.tar, a.tar_root)
    print('Staged local runtime inputs in', stage)


if __name__ == '__main__':
    main()
