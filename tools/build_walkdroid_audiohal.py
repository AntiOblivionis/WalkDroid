#!/usr/bin/env python3
"""Build the Android audio framework HAL and its death handler from source.

The Android source, generated HIDL headers, platform libraries and toolchain
are separate inputs. WalkDroid's A2DP routing change is compiled from the
current project source; no old modified audio HAL is copied or patched.
"""
from __future__ import annotations
import argparse
from walkdroid_build_metadata import write_optional, tool_version
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from walkdroid_generated_headers import discover

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--android-source', type=Path, required=True, help='caller-selected Android source/header view')
    ap.add_argument('--android-libs', type=Path, action='append', required=True, help='caller-selected platform library directory; repeat as needed')
    ap.add_argument('--generated-include', type=Path, action='append')
    ap.add_argument('--generated-root', type=Path, action='append', default=[], help='Android output tree containing generated include roots')
    ap.add_argument('--cc', type=Path, default=ROOT/'tools/cross-root/usr/bin/arm-linux-gnueabihf-g++-15')
    ap.add_argument('--binutils-dir', type=Path, default=ROOT/'tools/cross-root/usr/bin')
    ap.add_argument('--host-lib-dir', type=Path, default=ROOT/'tools/cross-root/usr/lib/x86_64-linux-gnu')
    ap.add_argument('--gcc-include', type=Path, default=ROOT/'tools/cross-root/usr/lib/gcc-cross/arm-linux-gnueabihf/15/include')
    ap.add_argument('--android-crt', type=Path, required=True, help='caller-selected Android CRT directory')
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    base = args.android_source
    libs = args.android_libs
    generated = (args.generated_include or []) + discover(args.generated_root)
    if args.output is None:
        args.output = ROOT/'build/audiohal'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    args.output.mkdir(parents=True, exist_ok=True)
    src = base/'frameworks_av__media_libaudiohal/2.0'
    includes = [
        'external_libcxx__include','external_libcxxabi__include',
        'bionic__libc_include','bionic__libc_kernel_uapi','bionic__libc_kernel_uapi/asm-arm',
        'bionic__libc_kernel_android_uapi','system_core__libutils_include',
        'system_core__libcutils_include','system_core__liblog_include','system_core__libsystem_include',
        'system_core__base/include','hardware_libhardware__include','system_media__audio_include',
        'system_media__audio_effects_include','system_media__audio_utils_include',
        'frameworks_av__media_libaudiohal_include','frameworks_av__media_libaudioclient_include',
        'frameworks_av__include','frameworks_av__media_libmedia_include','frameworks_av__media_libeffects_factory_include',
        'frameworks_av__media_libmediautils_include','frameworks_native__libs_nativebase_include',
        'frameworks_native__libs_nativewindow_include','frameworks_native__libs_ui_include',
        'frameworks_native__libs_binder_include','frameworks_native__opengl_include',
        'system_libhidl__base_include','system_libhidl__transport/include',
        'system_libhidl__libhidlmemory_include','system_libhwbinder__include','system_libfmq__include',
        'system_core__libsync/include','hardware_interfaces__audio/common/2.0/default',
        'hardware_interfaces__audio/common/all-versions/default/include',
    ]
    env = os.environ.copy()
    env['LD_LIBRARY_PATH'] = str(args.host_lib_dir) + ((':' + env['LD_LIBRARY_PATH']) if env.get('LD_LIBRARY_PATH') else '')
    source_names = ['DeviceHalLocal.cpp', 'DevicesFactoryHalHybrid.cpp', 'DevicesFactoryHalLocal.cpp',
                    'StreamHalLocal.cpp', 'ConversionHelperHidl.cpp', 'DeviceHalHidl.cpp',
                    'DevicesFactoryHalHidl.cpp', 'EffectBufferHalHidl.cpp', 'EffectHalHidl.cpp',
                    'EffectsFactoryHalHidl.cpp', 'StreamHalHidl.cpp']
    with tempfile.TemporaryDirectory(prefix='walkdroid-audiohal-') as temp:
        work = Path(temp)
        binutils = work/'bin'
        binutils.mkdir()
        for name in ('as','ld'):
            (binutils/name).symlink_to((args.binutils_dir/('arm-linux-gnueabihf-'+name)).resolve())
        compat = work/'compat/hidl'
        compat.mkdir(parents=True)
        header = (base/'system_libhidl__base_include/hidl/HidlSupport.h').read_text(encoding='utf-8')
        header = header.replace('static_assert(hidl_vec<T>::kOffsetOfBuffer == 0, "wrong offset");',
                                'static_assert(__builtin_offsetof(hidl_vec<T>, mBuffer) == 0, "wrong offset");')
        (compat/'HidlSupport.h').write_text(header, encoding='utf-8')
        paths = [src, work/'compat', *generated, *[base/p for p in includes]]
        common = [str(args.cc), '-B'+str(binutils)+'/', '-B'+str(args.binutils_dir)+'/',
                  '-mfloat-abi=softfp', '-mfpu=neon', '-march=armv7-a', '-fPIC', '-O1',
                  '-std=gnu++14', '-nostdinc', '-nostdinc++', '-nostdlib', '-fno-stack-protector',
                  '-fno-exceptions', '-fno-rtti', '-fno-gnu-unique', '-D__ANDROID__',
                  '-D__ANDROID_API__=28', '-DBINDER_IPC_32BIT', '-Wno-attributes',
                  '-Wno-deprecated-declarations', '-Wno-template-body', '-include', 'cstring', '-include', 'climits',
                  *['-I'+str(p) for p in paths], '-isystem', str(args.gcc_include)]
        libpaths = [work, *libs]
        search = [opt for p in libpaths for opt in ('-L'+str(p), '-Wl,-rpath-link='+str(p))]
        shared = ['-shared','-Wl,--hash-style=sysv','-Wl,--no-undefined',str(args.android_crt/'crtbegin_so.o')]
        common_libs = ['libc++.so','libbase.so','libcutils.so','libhardware.so','libhidlbase.so',
                       'libhidltransport.so','libhwbinder.so','liblog.so','libutils.so',
                       'libdl.so','libm.so','libc.so']

        def build(name, sources, dependencies):
            cmd = (common + shared + ['-Wl,-soname,'+name] + list(map(str, sources)) + search
                   + ['-l:'+library for library in dependencies] + ['-lgcc', '-o', str(work/name)])
            subprocess.run(cmd, env=env, check=True)

        build('libaudiohal_deathhandler.so', [base/'frameworks_av__media_libaudiohal/HalDeathHandlerHidl.cpp'],
              common_libs)
        sources = [ROOT/'system/zx300a/android/audiohal'/n if n == 'DevicesFactoryHalHybrid.cpp' else src/n for n in source_names]
        dependencies = ['libaudiohal_deathhandler.so','libaudioutils.so','libfmq.so',
                        'libhidlmemory.so','android.hardware.audio@2.0.so',
                        'android.hardware.audio.common@2.0.so','android.hardware.audio.common@2.0-util.so',
                        'android.hardware.audio.effect@2.0.so','android.hidl.allocator@1.0.so',
                        'android.hidl.memory@1.0.so','libmedia_helper.so','libmediautils.so', *common_libs]
        build('libaudiohal@2.0.so', sources, dependencies)
        for name in ('libaudiohal_deathhandler.so','libaudiohal@2.0.so'):
            shutil.copy2(work/name, args.output/name)
    version = tool_version([str(args.cc), '--version'], env=env)
    write_optional((args.output/'build.lock.json'), json.dumps({'compiler':str(args.cc), 'version':version,
        'android_source':str(base), 'generated_headers':list(map(str,generated)),
        'android_libraries':list(map(str,libs)), 'sources':source_names},indent=2)+'\n', encoding='utf-8')
    print(f'Built Android audio HAL in {args.output}')


if __name__ == '__main__':
    main()
