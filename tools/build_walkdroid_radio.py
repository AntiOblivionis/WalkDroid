#!/usr/bin/env python3
"""Build the Android Bluetooth/NFC HIDL provider from the current sources.

Linux/WSL only. Android platform headers, generated HIDL headers, libraries and
the compiler are supplied separately; no old WalkDroid executable is used.
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

ROOT=Path(__file__).resolve().parents[1]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc',type=Path,default=ROOT/'tools/cross-root/usr/bin/arm-linux-gnueabihf-gcc-15')
    ap.add_argument('--cxx',type=Path,default=ROOT/'tools/cross-root/usr/bin/arm-linux-gnueabihf-g++-15')
    ap.add_argument('--binutils-dir',type=Path,default=ROOT/'tools/cross-root/usr/bin')
    ap.add_argument('--host-lib-dir',type=Path,default=ROOT/'tools/cross-root/usr/lib/x86_64-linux-gnu')
    ap.add_argument('--gcc-include',type=Path,default=ROOT/'tools/cross-root/usr/lib/gcc-cross/arm-linux-gnueabihf/15/include')
    ap.add_argument('--android-headers',type=Path,required=True,help='caller-selected Android source/header view')
    ap.add_argument('--android-libs',type=Path,required=True,help='caller-selected Android platform library directory')
    ap.add_argument('--android-crt',type=Path,required=True,help='caller-selected Android CRT directory')
    ap.add_argument('--generated-include',type=Path,action='append',help='platform generated HIDL header directory; repeat as needed')
    ap.add_argument('--generated-root',type=Path,action='append',default=[],help='Android output tree containing generated include roots')
    ap.add_argument('--extra-include',type=Path,action='append',default=[])
    ap.add_argument('--output',type=Path)
    a=ap.parse_args()
    if a.output is None:a.output=ROOT/'build/radio'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    a.output.mkdir(parents=True,exist_ok=True)
    h=a.android_headers;src=ROOT/'system/zx300a'
    generated=(a.generated_include or [])+discover(a.generated_root)
    includes=['external_libcxx__include','bionic__libc_include','bionic__libc_kernel_uapi',
        'bionic__libc_kernel_uapi/asm-arm','bionic__libc_kernel_android_uapi','external_libcxxabi__include',
        'system_core__libutils_include','system_core__libcutils_include','system_core__liblog_include',
        'system_core__libsystem_include','system_core__base/include','hardware_libhardware__include',
        'frameworks_native__libs_nativebase_include','frameworks_native__libs_nativewindow_include',
        'frameworks_native__libs_ui_include','frameworks_native__libs_binder_include','frameworks_av__include',
        'system_libhidl__base_include','system_libhidl__transport/include','system_libhwbinder__include',
        'system_libfmq__include','system_core__libsync/include','frameworks_native__opengl_include',
        'frameworks_av__media_libstagefright_omx_include','frameworks_av__media_libstagefright_xmlparser_include']
    env=os.environ.copy();env['LD_LIBRARY_PATH']=str(a.host_lib_dir)+((':'+env['LD_LIBRARY_PATH']) if env.get('LD_LIBRARY_PATH') else '')
    with tempfile.TemporaryDirectory(prefix='walkdroid-radio-') as temp:
        work=Path(temp);binutils=work/'bin';binutils.mkdir()
        for n in ('as','ld'):(binutils/n).symlink_to((a.binutils_dir/('arm-linux-gnueabihf-'+n)).resolve())
        compat=work/'compat/hidl';compat.mkdir(parents=True)
        header=(h/'system_libhidl__base_include/hidl/HidlSupport.h').read_text(encoding='utf-8')
        header=header.replace('static_assert(hidl_vec<T>::kOffsetOfBuffer == 0, "wrong offset");',
                              'static_assert(__builtin_offsetof(hidl_vec<T>, mBuffer) == 0, "wrong offset");')
        (compat/'HidlSupport.h').write_text(header,encoding='utf-8')
        paths=[src,work/'compat',*generated,*a.extra_include,*[h/n for n in includes],
               *sorted((h/'hardware_interfaces__graphics').rglob('include'))]
        common=['-B'+str(binutils)+'/', '-B'+str(a.binutils_dir)+'/', '-mfloat-abi=softfp',
                '-mfpu=neon','-march=armv7-a','-fPIC','-O1','-fno-stack-protector',
                '-nostdlib','-nostdinc','-D__ANDROID__','-D__ANDROID_API__=28',
                '-DBINDER_IPC_32BIT','-Wno-attributes',*['-I'+str(p) for p in paths],
                '-isystem',str(a.gcc_include)]
        objects=[];c_sources=['sony_audio_bt_radio_owner.c','sony_audio_bt_driver_close.c','sony_audio_owner.c']
        for name in c_sources:
            obj=work/(name+'.o');subprocess.run([str(a.cc),*common,'-std=gnu11','-c',str(src/name),'-o',str(obj)],env=env,check=True);objects.append(obj)
        libs=['c','dl','m',':libc++.so','base','cutils','hardware','hidlbase','hidltransport',
              'hwbinder','log','sync','utils','binder','fmq','ui',':android.hardware.graphics.allocator@2.0.so',
              ':android.hardware.graphics.mapper@2.0.so',':android.hardware.graphics.composer@2.1.so',
              ':android.hardware.nfc@1.0.so',':android.hardware.bluetooth@1.0.so',':libstagefright_omx.so',
              ':libstagefright_xmlparser.so',':android.hardware.media.omx@1.0.so',
              ':android.hardware.graphics.bufferqueue@1.0.so',':libhidlmemory.so']
        binary=work/'zx9-radio-service'
        cmd=[str(a.cxx),*common,'-std=gnu++14','-nostdinc++','-fno-exceptions','-fno-rtti',
             '-fno-gnu-unique','-Wno-deprecated-declarations','-Wno-template-body','-include','cstring','-include','climits',
             '-pie','-Wl,--dynamic-linker=/system/bin/linker','-Wl,--hash-style=sysv','-Wl,--no-undefined',
             str(a.android_crt/'crtbegin_dynamic.o'),str(src/'radio_service.cpp'),*map(str,objects),
             '-L'+str(a.android_libs),'-Wl,-rpath-link='+str(a.android_libs),*['-l'+n for n in libs],
             '-lgcc',str(a.android_crt/'crtend_android.o'),'-o',str(binary)]
        subprocess.run(cmd,env=env,check=True);shutil.copy2(binary,a.output/binary.name)
    version=tool_version([str(a.cxx), '--version'], env=env)
    write_optional((a.output/'build.lock.json'), json.dumps({'compiler':str(a.cxx),'version':version,
        'android_headers':str(h),'generated_headers':list(map(str,generated)),
        'android_libraries':str(a.android_libs),'android_crt':str(a.android_crt),
        'sources':['radio_service.cpp',*c_sources]},indent=2)+'\n',encoding='utf-8')
    print(f'Built radio provider in {a.output}')


if __name__=='__main__':main()
