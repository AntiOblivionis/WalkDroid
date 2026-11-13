#!/usr/bin/env python3
"""Build the WalkDroid kernel from a supplied Sony source tree or archive.

Runs on Linux/WSL. Each run creates a temporary source tree, removes any prior
build output there, applies the current WalkDroid sources and compiles zImage.
No device access, flashing, historical binary or hash prerequisite is used.
"""
from __future__ import annotations
import argparse
from walkdroid_build_metadata import write_optional, tool_version
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from walkdroid_parallel_build import default_jobs

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    source = ap.add_mutually_exclusive_group(required=True)
    source.add_argument('--source-archive', type=Path, help='Sony kernel source archive')
    source.add_argument('--source-tree', type=Path, help='Sony kernel source directory')
    ap.add_argument('--strip-components', type=int, default=0, help='archive leading directories to remove')
    ap.add_argument('--config', type=Path, default=ROOT/'system/zx300a/kernel/zx300a.config')
    ap.add_argument('--cross-prefix', default=str(ROOT/'tools/gcc48/bin/arm-eabi-'))
    ap.add_argument('--ld', type=Path, default=ROOT/'tools/gcc48/bin/arm-eabi-ld.bfd')
    ap.add_argument('--size-tool', default='size')
    ap.add_argument('--jobs', type=int, default=default_jobs(24),
                    help='parallel kernel build jobs (default: available CPUs, up to 24)')
    ap.add_argument('--work-parent', type=Path, default=Path('/var/tmp'))
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    args.output = (args.output or ROOT/'build/kernel'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')).resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    src = ROOT/'system/zx300a'
    kernel = src/'kernel'
    patches = src/'kernel-patches'
    env = os.environ.copy()
    for key in ('KBUILD_OUTPUT', 'KBUILD_SRC'):
        env.pop(key, None)
    env.setdefault('KBUILD_BUILD_USER', 'walkdroid')
    env.setdefault('KBUILD_BUILD_HOST', 'walkdroid')
    env['PATH'] = str(Path(args.cross_prefix).parent) + ':' + env.get('PATH', '')
    with tempfile.TemporaryDirectory(prefix='walkdroid-kernel-', dir=args.work_parent) as temporary:
        # Sony's tree uses relative includes through a sibling named kernel.
        work = Path(temporary)/'kernel'
        if args.source_tree:
            shutil.copytree(args.source_tree.resolve(), work, symlinks=True)
        else:
            work.mkdir()
            subprocess.run(['tar', '-xf', str(args.source_archive.resolve()), '-C', str(work),
                            '--strip-components', str(args.strip_components)], check=True)
        # A caller may point at a previously built tree or an archive of one.
        # Drop generated objects in the private copy so they cannot become
        # inputs to this build; missing source is reported by the compiler.
        discarded = 0
        for path in work.rglob('*'):
            if path.name.endswith(('.o', '.a', '.ko', '.cmd', '.mod.c')) and (path.is_file() or path.is_symlink()):
                path.unlink()
                discarded += 1
        build = Path(temporary)/'objects'
        build.mkdir()
        # An out-of-tree Kbuild directory cannot reuse source-tree objects.
        # Remove only copied generated configuration markers if present.
        for marker in (work/'.config', work/'include/config/auto.conf', work/'include/generated/autoconf.h'):
            marker.unlink(missing_ok=True)
        common = ['make', '-C', str(work), 'O='+str(build), 'ARCH=arm', 'CROSS_COMPILE='+args.cross_prefix,
                  'LD='+str(args.ld), 'SIZE='+args.size_tool,
                  'MTK_TB_WIFI_3G_MODE=WIFI_ONLY', 'MTK_CM4_SUPPORT=yes', 'MTK_I2SOUT_KEEPCLK=no',
                  'HOSTCC=gcc -no-pie', 'HOSTCFLAGS=-O2 -fcommon -fno-pie', 'HOSTLDFLAGS=-no-pie']
        gadget = work/'drivers/usb/gadget'
        for name in ('android.c', 'f_uac2.c', 'f_fs.c'):
            shutil.copyfile(kernel/'usb'/name, gadget/name)
        shutil.copyfile(kernel/'usb/musb_gadget.c', work/'drivers/usb/host/usb20/musb_gadget.c')
        for name in ('sony_uac2_capture_boundary.inc', 'sony_uac2_lifetime.inc',
                     'sony_uac2_source_collect.inc', 'sony_uac2_source_device.inc',
                     'sony_uac2_source_feedback.inc', 'sony_uac2_source_commit.inc',
                     'sony_uac2_source_packet.inc'):
            shutil.copyfile(kernel/name, gadget/name)
        for name in ('sony_audio_usb_boundary.c', 'sony_audio_usb_boundary.h',
                     'sony_audio_usb_source_queue.c', 'sony_audio_usb_source_queue.h',
                     'sony_audio_usb_source_device.h'):
            shutil.copyfile(src/name, gadget/name)
        shutil.copyfile(kernel/'radio/wmt_conf.h',
                        work/'drivers/misc/mediatek/conn_soc/common/core/include/wmt_conf.h')
        partition = work/'arch/arm/mach-mt8590/BBDMP3_linux/common'
        partition.mkdir(parents=True, exist_ok=True)
        for name in ('partition_define.h', 'partition_define_private.h', 'pmt.h'):
            shutil.copyfile(kernel/'partition'/name, partition/name)
        for name in ('0006-android9-baseline.patch', '0005-arm-oabi-swi-pagefault.patch'):
            subprocess.run(['patch', '-p1', '--batch', '--forward', '-i', str((patches/name).resolve())],
                           cwd=work, check=True)
        mediatek = work/'drivers/misc/mediatek/Makefile'
        text = mediatek.read_text(encoding='utf-8')
        text = re.sub(r'(?m)^(obj-y\s*\+=\s*imgsensor/src)$',
                      r'ifneq ($(CONFIG_ARCH_MT8590_ICX),y)\n\1/\nendif', text)
        text = re.sub(r'(?m)^(obj-y\s*\+=\s*flashlight/src)$', r'\1/', text)
        mediatek.write_text(text, encoding='utf-8')
        compressed = work/'arch/arm/boot/compressed/Makefile'
        text = compressed.read_text(encoding='utf-8').replace('$(CROSS_COMPILE)size ', '$(SIZE) ')
        compressed.write_text(text, encoding='utf-8')
        shutil.copyfile(args.config.resolve(), build/'.config')
        include = [work/'drivers/staging/android/ion', work/'drivers/misc/mediatek/lcm']
        flags = 'KCFLAGS=' + ' '.join('-I'+str(p) for p in include)
        subprocess.run(common+[flags, 'olddefconfig'], env=env, check=True)
        produced_kernel_modules = []
        with (args.output/'build.log').open('w', encoding='utf-8') as log:
            subprocess.run(common+[flags, '-j'+str(args.jobs), 'zImage'], env=env,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
            modules_output = args.output/'modules'
            modules_output.mkdir(exist_ok=True)
            subprocess.run(common+[flags, '-j'+str(args.jobs), 'modules'], env=env,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
            for module in build.rglob('*.ko'):
                target = modules_output/'kernel'/module.relative_to(build)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(module, target)
                produced_kernel_modules.append(module.relative_to(build).as_posix())
            for name in ('zx9_aslr', 'zx9_hwbinder', 'zx9_zram', 'wm_key', 'icx_carddet'):
                module = Path(temporary)/name
                shutil.copytree(kernel/'modules'/name, module)
                subprocess.run(common+[flags, '-j'+str(args.jobs), 'M='+str(module), 'modules'],
                               env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
                shutil.copy2(module/(name+'.ko'), modules_output/(name+'.ko'))
        for source_path, output_name in ((build/'arch/arm/boot/zImage', 'zImage'),
                                         (build/'.config', 'config'), (build/'System.map', 'System.map'),
                                         (build/'Module.symvers', 'Module.symvers')):
            if source_path.exists():
                shutil.copy2(source_path, args.output/output_name)
    version = tool_version([args.cross_prefix+'gcc', '--version'], env=env)
    record = {'source_archive': str(args.source_archive.resolve()) if args.source_archive else None,
              'source_tree': str(args.source_tree.resolve()) if args.source_tree else None,
              'config': str(args.config.resolve()), 'compiler': args.cross_prefix+'gcc',
              'compiler_version': version, 'build_user': env['KBUILD_BUILD_USER'],
              'build_host': env['KBUILD_BUILD_HOST'], 'jobs': args.jobs,
              'discarded_prebuilt_objects_in_private_copy': discarded,
              'modules': ['zx9_aslr', 'zx9_hwbinder', 'zx9_zram', 'wm_key', 'icx_carddet'],
              'kernel_modules': produced_kernel_modules}
    write_optional((args.output/'build.lock.json'), json.dumps(record, indent=2)+'\n', encoding='utf-8')
    print(f'Built source kernel in {args.output}')


if __name__ == '__main__':
    main()
