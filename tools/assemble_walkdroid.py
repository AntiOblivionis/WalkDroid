#!/usr/bin/env python3
"""Assemble local WalkDroid images from fresh builds and the owner's files.

This is an offline tool. Its output contains the supplied proprietary files and
must not be included in the source repository or a public release. It does not
read from, select, reboot, or write to a player.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--product', type=Path, required=True,
                        help='fresh Android target/product/zx300a output')
    parser.add_argument('--components', type=Path, required=True,
                        help='fresh component results directory, including sony-runtime')
    parser.add_argument('--kernel', type=Path, required=True, help='fresh kernel output directory')
    parser.add_argument('--busybox', type=Path, required=True, help='fresh BusyBox executable')
    parser.add_argument('--vendor-system', type=Path, required=True,
                        help='private extraction of the owner\'s Walkman One system image')
    parser.add_argument('--vendor-boot', type=Path, required=True,
                        help='private extraction of the owner\'s original boot ramdisk')
    parser.add_argument('--mali-lib', type=Path, required=True,
                        help='locally supplied compatible Mali userspace library')
    parser.add_argument('--work', type=Path, required=True, help='new private working directory')
    parser.add_argument('--output', type=Path, required=True, help='private local image directory')
    parser.add_argument('--data-size-bytes', default='6848774144')
    parser.add_argument('--sony-ai-libs', type=Path, action='append', default=[],
                        help='optional separately supplied runtime libraries')
    args = parser.parse_args()

    product = args.product.resolve()
    components = args.components.resolve()
    vendor = args.vendor_system.resolve()
    boot = args.vendor_boot.resolve()
    work = args.work.resolve()
    images = args.output.resolve()
    work.mkdir(parents=True, exist_ok=False)
    images.mkdir(parents=True, exist_ok=True)
    (work / 'tmp').mkdir()

    def run(script, *values):
        print('Running', script, flush=True)
        subprocess.run([sys.executable, str(ROOT / 'tools' / script), *map(str, values)], check=True)

    # The radio runtime searches the original boot libraries first, as they
    # contain the glibc loader used by the stock launcher.
    run('prepare_native_radio_runtime.py', '--launcher', vendor / 'bin/6620_launcherA',
        '--lib-dir', boot / 'lib', '--lib-dir', vendor / 'lib',
        '--output', work / 'radio-native')

    boot_args = []
    for name in ('native', 'graphics', 'audio-services', 'radio', 'policy', 'audiohal'):
        boot_args += ['--' + name + '-output', components / name]
    boot_args += [
        '--kernel-output', args.kernel,
        '--settings-apk', product / 'system/app/WalkDroidSonyAudio/WalkDroidSonyAudio.apk',
        '--busybox', args.busybox, '--android-init', product / 'root/init',
        '--adbd', product / 'system/bin/adbd', '--sepolicy', product / 'root/sepolicy',
        '--policy-contexts', product / 'root',
        '--platform-default-prop', product / 'root/default.prop',
        '--mali-lib', args.mali_lib, '--radio-native-output', work / 'radio-native',
        '--dac-assets', vendor / 'usr/share/audio_dac',
        '--gain-assets', vendor / 'etc/.mod/gain',
    ]
    for name in ('ROMv2_lm_patch_1_0_hdr.bin', 'ROMv2_lm_patch_1_1_hdr.bin',
                 'WIFI_RAM_CODE_8590', 'WMT_SOC.cfg'):
        boot_args += ['--asset', 'sony/radio/firmware/' + name, vendor / 'etc/firmware' / name]
    run('stage_walkdroid_boot.py', *boot_args, '--output', work / 'boot-stage')
    run('build_walkdroid_boot.py', '--page-size', '2048', '--kernel-address', '0x80008000',
        '--ramdisk-address', '0x84000000', '--tags-address', '0x80000100',
        '--cmdline', 'androidboot.selinux=permissive', '--mtk-headers',
        '--kernel', args.kernel / 'zImage',
        '--overlay', ROOT / 'system/zx300a/ramdisk/entries.json',
        '--overlay', work / 'boot-stage/entries.json', '--pad-to', '16777216',
        '--output', images / 'boot.img')

    data_args = ['--native-output', components / 'native',
                 '--sony-output', components / 'sony-runtime']
    for library_dir in (vendor / 'lib', vendor / 'vendor/sony/lib', boot / 'lib'):
        data_args += ['--sony-libs', library_dir]
    for name in ('libbtcust_tab.so', 'libbtdrv.so', 'libcustom_nvram.so', 'libhwm.so', 'libnfctools.so'):
        data_args += ['--sony-lib-override', name, vendor / 'lib' / name]
    for name in ('libeaselqt.so', 'libInitialObject.so', 'libKeyListener.so',
                 'libMediaStoreService.so', 'libMediaStoreServiceClient.so',
                 'libOneTrackPlayerService.so', 'libPlayerService.so', 'libRecorderService.so',
                 'libScreenshotShared.so', 'libSoundServiceFw.so'):
        data_args += ['--sony-lib-override', name, vendor / 'vendor/sony/lib' / name]
    for dest, path in (
        ('etc/asound.conf', vendor / 'etc/asound.conf'),
        ('usr/share/alsa', vendor / 'usr/share/alsa'),
        ('usr/share/audio_dac', vendor / 'usr/share/audio_dac'),
        ('system/vendor/sony/etc/effector_params', vendor / 'vendor/sony/etc/effector_params'),
        ('system/vendor/sony/etc/audioanalyzer_params', vendor / 'vendor/sony/etc/audioanalyzer_params'),
        ('opt/walkdroid/profiles/analyzer', vendor / 'etc/.mod/anls'),
        ('opt/walkdroid/profiles/adler', vendor / 'etc/.mod/adler'),
        ('opt/walkdroid/profiles/gain', vendor / 'etc/.mod/gain'),
    ):
        data_args += ['--asset', dest, path]
    for path in args.sony_ai_libs:
        data_args += ['--sony-ai-libs', path]
    run('stage_walkdroid_runtime.py', *data_args, '--mtkbt', vendor / 'bin/mtkbt',
        '--synthetic-snapshot', '--output', work / 'runtime-stage',
        '--tar', work / 'runtime-stage.tar', '--tar-root', '.')
    run('build_walkdroid_data_image.py', '--data-tree', work / 'runtime-stage/data',
        '--size-bytes', args.data_size_bytes, '--metadata-tar', work / 'runtime-stage.tar',
        '--features', '^64bit,^metadata_csum,^orphan_file', '--work-dir', work / 'tmp',
        '--output', images / 'data.img')
    shutil.copyfile(product / 'system.img', images / 'system.img')
    print('Private images assembled:', images, flush=True)
    print('These images contain vendor material. Do not publish them.', flush=True)


if __name__ == '__main__':
    main()
