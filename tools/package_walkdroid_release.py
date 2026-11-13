#!/usr/bin/env python3
"""Stage a source-built open-component release without firmware or keys.

The builder selects fresh output directories. This does not extract, select,
or bundle Sony firmware, a Mali driver, pairing data, or private local keys.
Distribution of corresponding source and licence notices remains necessary.
"""
import argparse
from pathlib import Path
import shutil


ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--product', type=Path, required=True)
    p.add_argument('--components', type=Path, required=True)
    p.add_argument('--kernel', type=Path, required=True)
    p.add_argument('--busybox', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    destination = a.output.resolve()
    destination.mkdir(parents=True, exist_ok=False)

    def copy(source, target):
        target = destination / target
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)

    for name in ('LICENSE', 'NOTICE', 'README.md', 'README.zh-TW.md', 'README.en.md', 'README.ja.md'):
        copy(ROOT / name, name)
    copy(ROOT / 'assets/walkdroid/logo.png', 'assets/walkdroid/logo.png')
    copy(ROOT / 'assets/walkdroid/fonts/OFL.txt', 'assets/walkdroid/fonts/OFL.txt')
    for name in ('Apache-2.0.txt', 'BusyBox-LICENSE.txt', 'LGPL-2.0.txt', 'LGPL-2.1.txt',
                 'GPL-3.0.txt', 'GCC-Runtime-Exception-3.1.txt'):
        copy(ROOT / 'licenses' / name, 'licenses/' + name)
    for name in ('BUILDING.md', 'BUILDING.zh-TW.md', 'BUILDING.en.md', 'BUILDING.ja.md',
                 'INSTALL.md', 'INSTALL.zh-TW.md', 'INSTALL.en.md', 'INSTALL.ja.md'):
        copy(ROOT / 'docs' / name, 'docs/' + name)
    for name in ('assemble_walkdroid.py', 'extract_walkman_device.py',
                 'extract_walkdroid_boot_resources.py', 'prepare_native_radio_runtime.py',
                 'stage_walkdroid_boot.py', 'build_walkdroid_boot.py',
                 'stage_walkdroid_runtime.py', 'build_walkdroid_data_image.py',
                 'cpio_newc.py', 'walkdroid_build_metadata.py', 'install_walkdroid.py',
                 'ramboot_layout.py'):
        copy(ROOT / 'tools' / name, 'tools/' + name)
    shutil.copytree(ROOT / 'system/zx300a/ramdisk', destination / 'system/zx300a/ramdisk',
                    ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
    copy(ROOT / 'system/zx300a/audio_policy_configuration.xml',
         'system/zx300a/audio_policy_configuration.xml')
    for component in ('audiohal', 'exfat', 'graphics', 'launcher', 'storage'):
        copy(ROOT / 'system/zx300a/android' / component / 'NOTICE.md',
             'system/zx300a/android/' + component + '/NOTICE.md')
    copy(ROOT / 'system/zx300a/android/graphics/APACHE-2.0.txt',
         'system/zx300a/android/graphics/APACHE-2.0.txt')
    copy(ROOT / 'system/zx300a/kernel/modules/icx_carddet/NOTICE.md',
         'system/zx300a/kernel/modules/icx_carddet/NOTICE.md')
    copy(ROOT / 'system/zx300a/kernel/COPYING', 'kernel/COPYING')

    for name in ('system.img', 'root/init', 'root/sepolicy', 'root/default.prop',
                 'system/bin/adbd',
                 'system/app/WalkDroidSonyAudio/WalkDroidSonyAudio.apk',
                 'system/etc/NOTICE.html.gz'):
        copy(a.product / name, 'platform/' + name)
    for context in (a.product / 'root').glob('*_contexts'):
        copy(context, 'platform/root/' + context.name)
    for folder in ('native', 'sony-runtime', 'audiohal', 'radio', 'graphics', 'audio-services', 'policy'):
        shutil.copytree(a.components / folder, destination / 'components' / folder,
                        ignore=shutil.ignore_patterns('build.lock.json', '*.log', '__pycache__'))
    copy(a.kernel / 'zImage', 'kernel/zImage')
    for name in ('wm_key', 'zx9_aslr', 'zx9_hwbinder', 'zx9_zram', 'icx_carddet'):
        copy(a.kernel / 'modules' / (name + '.ko'), 'kernel/modules/' + name + '.ko')
    for name in ('drivers/misc/mediatek/common_detect/mtk_wmt_detect.ko',
                 'drivers/misc/mediatek/conn_soc/common/mtk_stp_wmt_soc.ko',
                 'drivers/misc/mediatek/conn_soc/common/mtk_stp_bt_soc.ko'):
        copy(a.kernel / 'modules/kernel' / name, 'kernel/modules/kernel/' + name)
    copy(a.busybox, 'busybox/busybox')
    busybox_config = a.busybox.parent / 'busybox.config'
    if busybox_config.is_file():
        copy(busybox_config, 'busybox/busybox.config')
    print('Staged open-component release:', destination)
    print('Keep the corresponding source distribution and third-party notices available with it.')


if __name__ == '__main__':
    main()
