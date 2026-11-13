#!/usr/bin/env python3
"""Build the graphics, display and media-service layer from current sources.

Android platform sources, generated HIDL headers, libraries and the compiler
are supplied independently. No previous WalkDroid binary is an input.
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
import tarfile
import tempfile
from walkdroid_generated_headers import discover

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--android-source', type=Path, required=True, help='caller-selected Android source/header view')
    ap.add_argument('--generated-include', type=Path, action='append')
    ap.add_argument('--generated-root', type=Path, action='append', default=[], help='Android output tree containing generated include roots')
    ap.add_argument('--android-libs', type=Path, action='append', required=True, help='caller-selected platform library directory; repeat as needed')
    ap.add_argument('--cc', type=Path, default=ROOT/'tools/cross-root/usr/bin/arm-linux-gnueabihf-gcc-15')
    ap.add_argument('--cxx', type=Path, default=ROOT/'tools/cross-root/usr/bin/arm-linux-gnueabihf-g++-15')
    ap.add_argument('--binutils-dir', type=Path, default=ROOT/'tools/cross-root/usr/bin')
    ap.add_argument('--host-lib-dir', type=Path, default=ROOT/'tools/cross-root/usr/lib/x86_64-linux-gnu')
    ap.add_argument('--gcc-include', type=Path, default=ROOT/'tools/cross-root/usr/lib/gcc-cross/arm-linux-gnueabihf/15/include')
    ap.add_argument('--android-crt', type=Path, required=True, help='caller-selected Android CRT directory')
    ap.add_argument('--vendor-display-dir', type=Path,
                    help='optional display interface headers; default: bundled AOSP CTS headers')
    ap.add_argument('--kernel-source', type=Path,
                    help='caller-provided Sony kernel source archive or checkout containing the display headers')
    ap.add_argument('--vendor-display-relative-dir', default='drivers/misc/mediatek/videox/mt8590',
                    help='display header directory inside the selected kernel source; may be overridden')
    ap.add_argument('--ion-heap-mask', default='1024')
    ap.add_argument('--output', type=Path)
    a = ap.parse_args()
    if a.output is None:
        a.output = ROOT/'build/graphics'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    a.output.mkdir(parents=True, exist_ok=True)
    h = a.android_source
    source = ROOT/'system/zx300a/android/graphics'
    graphics = h/'hardware_interfaces__graphics'
    generated = (a.generated_include or []) + discover(a.generated_root)
    libs = a.android_libs
    includes = [
        'external_libcxx__include','external_libcxxabi__include','bionic__libc_include',
        'bionic__libc_kernel_uapi','bionic__libc_kernel_uapi/asm-arm','bionic__libc_kernel_android_uapi',
        'system_core__libutils_include','system_core__libcutils_include','system_core__liblog_include',
        'system_core__libsystem_include','system_core__base/include','system_core__libion/include',
        'system_core__libion/kernel-headers','hardware_libhardware__include',
        'frameworks_native__libs_nativebase_include','frameworks_native__libs_nativewindow_include',
        'frameworks_native__libs_ui_include','frameworks_native__libs_binder_include',
        'frameworks_native__opengl_include','frameworks_av__include','system_libhidl__base_include','system_libhidl__transport/include',
        'system_libhwbinder__include','system_libfmq__include','system_core__libsync/include',
        'frameworks_av__media_libstagefright_omx_include','frameworks_av__media_libstagefright_xmlparser_include',
        'hardware_interfaces__configstore/1.1/default','system_core__libpackagelistparser_include',
        'external_minijail__','oreo_sdcard',
    ]
    env = os.environ.copy()
    env['LD_LIBRARY_PATH'] = str(a.host_lib_dir) + ((':' + env['LD_LIBRARY_PATH']) if env.get('LD_LIBRARY_PATH') else '')
    built = []
    with tempfile.TemporaryDirectory(prefix='walkdroid-graphics-') as temp:
        work = Path(temp)
        vendor_headers = a.vendor_display_dir
        if vendor_headers is None and a.kernel_source is None:
            vendor_headers = ROOT/'system/zx300a/android/graphics/mtk-uapi'
        if vendor_headers is None and a.kernel_source is not None:
            if a.kernel_source.is_dir():
                vendor_headers = a.kernel_source/a.vendor_display_relative_dir
            else:
                vendor_headers = work/'vendor-display'
                vendor_headers.mkdir()
                wanted = {name: (a.vendor_display_relative_dir.rstrip('/')+'/'+name).lstrip('/')
                          for name in ('mtkfb.h', 'mtkfb_info.h')}
                # Python versions before 3.14 do not read zstd tar archives.
                # Stream through the standard zstd tool when the caller chose
                # that format; ordinary tar formats remain handled by tarfile.
                decompressor = None
                if a.kernel_source.name.endswith(('.tar.zst', '.tzst')):
                    decompressor = subprocess.Popen(['zstd', '-dc', str(a.kernel_source)], stdout=subprocess.PIPE)
                    archive = tarfile.open(fileobj=decompressor.stdout, mode='r|')
                else:
                    archive = tarfile.open(a.kernel_source, mode='r|*')
                found = set()
                try:
                    with archive:
                        for member in archive:
                            if not member.isfile():
                                continue
                            actual = member.name.lstrip('./')
                            for name, requested in wanted.items():
                                if name not in found and (actual == requested or actual.endswith('/'+requested)):
                                    with archive.extractfile(member) as source_file, (vendor_headers/name).open('wb') as target:
                                        shutil.copyfileobj(source_file, target)
                                    found.add(name)
                    if decompressor and decompressor.wait():
                        raise RuntimeError('Could not decompress the supplied kernel source archive')
                finally:
                    if decompressor and decompressor.poll() is None:
                        decompressor.terminate()
                        decompressor.wait()
                if found != set(wanted):
                    raise FileNotFoundError('display headers not found in selected kernel source: '+', '.join(sorted(set(wanted)-found)))
        binutils = work/'bin'
        binutils.mkdir()
        for name in ('as','ld'):
            (binutils/name).symlink_to((a.binutils_dir/('arm-linux-gnueabihf-'+name)).resolve())
        compat = work/'compat/hidl'
        compat.mkdir(parents=True)
        header = (h/'system_libhidl__base_include/hidl/HidlSupport.h').read_text(encoding='utf-8')
        # GCC cannot use this particular external non-constexpr expression.
        header = header.replace('static_assert(hidl_vec<T>::kOffsetOfBuffer == 0, "wrong offset");', '')
        (compat/'HidlSupport.h').write_text(header, encoding='utf-8')
        paths = [source/'gralloc', work/'compat', *([vendor_headers] if vendor_headers else []),
                 *generated, *[h/p for p in includes],
                 *sorted(graphics.rglob('include'))]
        common = ['-B'+str(binutils)+'/', '-B'+str(a.binutils_dir)+'/', '-mfloat-abi=softfp',
                  '-mfpu=neon','-march=armv7-a','-fPIC','-O1','-fno-stack-protector',
                  '-nostdlib','-nostdinc','-D__ANDROID__','-D__ANDROID_API__=28','-DBINDER_IPC_32BIT',
                  '-DLOG_TAG="WalkDroidGraphics"',
                  '-Wno-attributes',*['-I'+str(p) for p in paths],'-isystem',str(a.gcc_include)]
        cxx = ['-std=gnu++14','-nostdinc++','-fno-exceptions','-fno-rtti','-fno-gnu-unique',
               '-Wno-deprecated-declarations','-Wno-template-body','-include','cstring','-include','climits']
        system_libs = ['libc.so','libdl.so','libm.so','libc++.so','libbase.so','libcutils.so',
                       'libhardware.so','libhidlbase.so','libhidltransport.so','libhwbinder.so',
                       'liblog.so','libsync.so','libutils.so','libbinder.so','libfmq.so','libui.so',
                       'android.hardware.graphics.allocator@2.0.so','android.hardware.graphics.mapper@2.0.so',
                       'android.hardware.graphics.composer@2.1.so']
        search = [option for path in (work,*libs) for option in ('-L'+str(path),'-Wl,-rpath-link='+str(path))]

        def build(name, sources, dependencies, executable=False, defines=(), c=False):
            driver = a.cc if c else a.cxx
            language = ['-std=gnu11'] if c else cxx
            kind = (['-pie','-Wl,--dynamic-linker=/system/bin/linker',str(a.android_crt/'crtbegin_dynamic.o')]
                    if executable else ['-shared','-Wl,-soname,'+name,str(a.android_crt/'crtbegin_so.o')])
            cmd = [str(driver),*common,*language,*['-D'+item for item in defines],*kind,
                   '-Wl,--hash-style=sysv','-Wl,--no-undefined',*map(str,sources),*search,
                   *['-l:'+library for library in dependencies],'-lgcc']
            if executable:cmd.append(str(a.android_crt/'crtend_android.o'))
            cmd += ['-o',str(work/name)]
            subprocess.run(cmd,env=env,check=True)
            built.append({'output':name,'sources':list(map(str,sources)),'libraries':dependencies})
            print('built',name,flush=True)

        for name, paths, executable in [
            ('libhwc2onfbadapter.so',['composer/2.1/utils/hwc2onfbadapter/HWC2OnFbAdapter.cpp'],False),
            ('libhwc2on1adapter.so',['composer/2.1/utils/hwc2on1adapter/HWC2On1Adapter.cpp',
                                    'composer/2.1/utils/hwc2on1adapter/MiniFence.cpp'],False),
            ('android.hardware.graphics.allocator@2.0-impl.so',['allocator/2.0/default/passthrough.cpp'],False),
            ('android.hardware.graphics.mapper@2.0-impl.so',['mapper/2.0/default/passthrough.cpp'],False),
            ('android.hardware.graphics.composer@2.1-impl.so',['composer/2.1/default/passthrough.cpp'],False),
            ('android.hardware.graphics.allocator@2.0-service',['allocator/2.0/default/service.cpp'],True),
            ('android.hardware.graphics.composer@2.1-service',['composer/2.1/default/service.cpp'],True),
        ]:
            deps = (['libhwc2onfbadapter.so','libhwc2on1adapter.so'] if 'composer@2.1-impl' in name else []) + system_libs
            build(name,[graphics/path for path in paths],deps,executable)

        config_deps = ['android.hardware.configstore@1.0.so','android.hardware.configstore@1.1.so',*system_libs]
        build('zx9-configstore-service', [source/'configstore_service.cpp',
              h/'hardware_interfaces__configstore/1.1/default/SurfaceFlingerConfigs.cpp'], config_deps, True,
              ['RUNNING_WITHOUT_SYNC_FRAMEWORK','USE_CONTEXT_PRIORITY=0','NUM_FRAMEBUFFER_SURFACE_BUFFERS=2',
               'USE_HWC_FOR_RGB_TO_YUV=0','TARGET_HAS_WIDE_COLOR_DISPLAY=0','TARGET_HAS_HDR_DISPLAY=0'])
        codec_deps = ['libstagefright_omx.so','libstagefright_xmlparser.so','android.hardware.media.omx@1.0.so',
                      'android.hardware.graphics.bufferqueue@1.0.so','libhidlmemory.so',*system_libs]
        build('zx9-codec-service',[source/'codec/codec_service.cpp'],codec_deps,True)
        gralloc_deps = ['libc.so','libdl.so','libm.so','liblog.so','libcutils.so','libhardware.so',
                        'libion.so','libGLESv1_CM.so','libc++.so']
        build('gralloc.default.so',[source/'gralloc'/name for name in
              ('gralloc_module.cpp','alloc_device.cpp','framebuffer_device.cpp')],gralloc_deps,False,
              ['GRALLOC_32_BITS','STANDARD_LINUX_SCREEN','PLATFORM_SDK_VERSION=28',
               'ZX300A_ION_MM_HEAP_MASK='+a.ion_heap_mask])
        build('fb-format-probe',[source/'fb_format.c'],['libc.so'],True,c=True)
        build('zx9-sdcard-fuse',[ROOT/'system/zx300a/android/storage/sdcard-fuse-compat.cpp',
              h/'oreo_sdcard/fuse.cpp'],['libminijail.so','libpackagelistparser.so',*system_libs],True)
        build('libzx9_healthd_uevent_filter.so',[ROOT/'system/zx300a/healthd_uevent_filter.c'],
              ['libc.so','libdl.so','liblog.so'],c=True)
        for item in built:
            shutil.copy2(work/item['output'],a.output/item['output'])
    version = tool_version([str(a.cxx), '--version'], env=env)
    write_optional((a.output/'build.lock.json'), json.dumps({'compiler':str(a.cxx),'version':version,
        'android_source':str(h),'generated_headers':list(map(str,generated)),
        'vendor_display_dir':str(a.vendor_display_dir) if a.vendor_display_dir else None,
        'kernel_source':str(a.kernel_source) if a.kernel_source else None,
        'android_libraries':list(map(str,libs)),'targets':built},indent=2)+'\n',encoding='utf-8')
    print('Built',len(built),'graphics/media components in',a.output)


if __name__ == '__main__':
    main()
