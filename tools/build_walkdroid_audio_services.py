#!/usr/bin/env python3
"""Build Android audio, lights and software-keymaster services from source.

The Android source tree, generated HIDL headers, platform libraries and cross
compiler are caller inputs. Objects are compiled in a fresh temporary directory.
"""
from __future__ import annotations
import argparse
from walkdroid_build_metadata import write_optional, tool_version
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from walkdroid_generated_headers import discover

ROOT=Path(__file__).resolve().parents[1]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--android-source',type=Path,required=True,help='caller-selected Android source/header view')
    ap.add_argument('--generated-include',type=Path,action='append')
    ap.add_argument('--generated-root',type=Path,action='append',default=[],help='Android output tree containing generated include roots')
    ap.add_argument('--android-libs',type=Path,action='append',required=True,help='caller-selected platform library directory; repeat as needed')
    ap.add_argument('--cc',type=Path,default=ROOT/'tools/cross-root/usr/bin/arm-linux-gnueabihf-gcc-15')
    ap.add_argument('--cxx',type=Path,default=ROOT/'tools/cross-root/usr/bin/arm-linux-gnueabihf-g++-15')
    ap.add_argument('--binutils-dir',type=Path,default=ROOT/'tools/cross-root/usr/bin')
    ap.add_argument('--host-lib-dir',type=Path,default=ROOT/'tools/cross-root/usr/lib/x86_64-linux-gnu')
    ap.add_argument('--gcc-include',type=Path,default=ROOT/'tools/cross-root/usr/lib/gcc-cross/arm-linux-gnueabihf/15/include')
    ap.add_argument('--android-crt',type=Path,required=True,help='caller-selected Android CRT directory')
    ap.add_argument('--output',type=Path)
    a=ap.parse_args()
    output=a.output or ROOT/'build/audio-services'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    output.mkdir(parents=True,exist_ok=True)
    h=a.android_source;audio=h/'hardware_interfaces__audio';keymaster=h/'system_keymaster__'
    generated=(a.generated_include or [])+discover(a.generated_root)
    libraries=a.android_libs
    env=os.environ.copy();env['LD_LIBRARY_PATH']=str(a.host_lib_dir)+((':'+env['LD_LIBRARY_PATH']) if env.get('LD_LIBRARY_PATH') else '')
    includes=['external_libcxx__include','external_libcxxabi__include','bionic__libc_include',
        'bionic__libc_kernel_uapi','bionic__libc_kernel_uapi/asm-arm','bionic__libc_kernel_android_uapi',
        'system_core__libutils_include','system_core__libcutils_include','system_core__liblog_include',
        'system_core__libsystem_include','system_core__base/include','hardware_libhardware__include',
        'frameworks_native__libs_nativebase_include','frameworks_native__libs_nativewindow_include',
        'frameworks_native__libs_ui_include','frameworks_native__libs_binder_include',
        'frameworks_native__opengl_include','system_libhidl__base_include','system_libhidl__transport/include',
        'system_libhwbinder__include','system_libfmq__include','system_core__libsync/include',
        'aaudio-include','external_tinyalsa/include','system_media__audio_include','system_media__audio_utils_include','frameworks_av__include',
        'system_media__audio_effects_include','frameworks_av__media_libmedia/include',
        'frameworks_av__media_libaudioclient_include','frameworks_av__media_libeffects_factory/include',
        'frameworks_av__media_libeffects_factory/include/media','frameworks_av__media_libeffects_config_include',
        'system_libhidl__libhidlmemory_include','system_keymaster__/include','system_keymaster__/ng/include',
        'external_boringssl__src_include']
    extra=[audio,audio/'core/all-versions/default/include/core/all-versions/default',
           audio/'effect/all-versions/default/include/effect/all-versions/default',
           audio/'common/2.0/default',audio/'core/2.0/default/include',audio/'effect/2.0/default/include',
           audio/'core/all-versions/default/include',audio/'common/all-versions/util/include',
           audio/'effect/all-versions/default/include',*sorted(audio.rglob('include')),
           *sorted((h/'hardware_interfaces__graphics').rglob('include'))]
    common_libs=['libc.so','libdl.so','libm.so','libc++.so','libbase.so','libcutils.so','libhardware.so',
        'libhidlbase.so','libhidltransport.so','libhwbinder.so','liblog.so','libsync.so','libutils.so',
        'libbinder.so','libfmq.so','libui.so','android.hardware.graphics.allocator@2.0.so',
        'android.hardware.graphics.mapper@2.0.so','android.hardware.graphics.composer@2.1.so',
        'libminijail.so','libpackagelistparser.so','libaaudio.so','android.hardware.light@2.0.so',
        'libeffectsconfig.so','android.hardware.audio@2.0.so','android.hardware.audio.common@2.0.so',
        'android.hardware.audio.effect@2.0.so','android.hardware.audio.common@2.0-util.so',
        'android.hardware.audio.common-util.so','libeffects.so','libhidlmemory.so','android.hidl.memory@1.0.so',
        'libcrypto.so','libsoftkeymasterdevice.so','libkeymaster_portable.so','libkeymaster_messages.so',
        'android.hardware.keymaster@3.0.so']
    built=[]
    with tempfile.TemporaryDirectory(prefix='walkdroid-audio-services-') as temp:
        work=Path(temp);bins=work/'bin';bins.mkdir()
        for name in ('as','ld'):(bins/name).symlink_to((a.binutils_dir/('arm-linux-gnueabihf-'+name)).resolve())
        compat=work/'compat';(compat/'hidl').mkdir(parents=True)
        hidl=(h/'system_libhidl__base_include/hidl/HidlSupport.h').read_text(encoding='utf-8')
        (compat/'hidl/HidlSupport.h').write_text(hidl.replace('static_assert(hidl_vec<T>::kOffsetOfBuffer == 0, "wrong offset");',''),encoding='utf-8')
        for name in ('StreamIn','StreamOut'):
            rel=Path('core/all-versions/default')/(name+'.impl.h')
            data=(audio/'core/all-versions/default/include'/rel).read_text(encoding='utf-8')
            data=data.replace('std::atomic_load_explicit(mStop, std::memory_order_acquire)','mStop->load(std::memory_order_acquire)')
            dest=compat/rel;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text(data,encoding='utf-8')
        rel=Path('effect/all-versions/default/Effect.impl.h')
        data=(audio/'effect/all-versions/default/include'/rel).read_text(encoding='utf-8')
        data=re.sub(r'std::atomic_load_explicit\((m\w+), (std::memory_order_\w+)\)',r'\1->load(\2)',data)
        dest=compat/rel;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text(data,encoding='utf-8')
        paths=[compat,*generated,*[h/path for path in includes],*extra]
        common=['-B'+str(bins)+'/', '-B'+str(a.binutils_dir)+'/','-mfloat-abi=softfp','-mfpu=neon',
            '-march=armv7-a','-fPIC','-O1','-fno-stack-protector','-nostdlib','-nostdinc',
            '-D__ANDROID__','-D__ANDROID_API__=28','-DAUDIO_HAL_VERSION_2_0','-DBINDER_IPC_32BIT',
            '-DLOG_TAG="WalkDroidServices"','-Wno-attributes',*['-I'+str(path) for path in paths],'-isystem',str(a.gcc_include)]
        cpp=['-std=gnu++14','-nostdinc++','-fno-exceptions','-fno-rtti','-fno-gnu-unique',
             '-Wno-deprecated-declarations','-Wno-template-body','-include','cstring','-include','climits']
        search=[flag for path in (work,*libraries) for flag in ('-L'+str(path),'-Wl,-rpath-link='+str(path))]

        def build(name,sources,dependencies=()):
            objects=[]
            for index,source in enumerate(sources):
                obj=work/(name+'-'+str(index)+'.o');is_c=source.suffix=='.c'
                cmd=[str(a.cc if is_c else a.cxx),*common,*(['-std=gnu11'] if is_c else cpp),'-c',str(source),'-o',str(obj)]
                subprocess.run(cmd,env=env,check=True);objects.append(obj)
            exe=not name.endswith('.so')
            kind=(['-pie','-Wl,--dynamic-linker=/system/bin/linker',str(a.android_crt/'crtbegin_dynamic.o')]
                  if exe else ['-shared','-Wl,-soname,'+name,str(a.android_crt/'crtbegin_so.o')])
            libs=[lib for lib in common_libs if lib!=name]
            cmd=[str(a.cxx),*common,*cpp,*kind,*map(str,objects),*search,'-Wl,--hash-style=sysv,--no-undefined,--as-needed',
                 *['-l:'+lib for lib in (*dependencies,*libs)],'-lgcc']
            if exe:cmd.append(str(a.android_crt/'crtend_android.o'))
            cmd+=['-o',str(work/name)];subprocess.run(cmd,env=env,check=True)
            built.append({'output':name,'sources':list(map(str,sources))});print('built',name,flush=True)

        factory=h/'frameworks_av__media_libeffects_factory'
        build('libeffects.so',[factory/name for name in ('EffectsFactory.c','EffectsConfigLoader.c','EffectsFactoryState.c','EffectsXmlConfigLoader.cpp')])
        build('android.hardware.audio@2.0-impl.so',sorted((audio/'core/2.0/default').glob('*.cpp'))+
              [h/'frameworks_av__media_libmedia'/name for name in ('AudioParameter.cpp','TypeConverter.cpp','TimeCheck.cpp')])
        build('android.hardware.audio.effect@2.0-impl.so',sorted((audio/'effect/2.0/default').glob('*.cpp')))
        build('zx9-audio-service',[ROOT/'system/zx300a/android/audio/audio_service.cpp'])
        light=h/'hardware_interfaces__light/2.0/default'
        build('android.hardware.light@2.0-impl.so',[light/'Light.cpp'])
        build('zx9-light-service',[light/'service.cpp'])
        bp=(keymaster/'Android.bp').read_text(encoding='utf-8')
        for module in ('libpuresoftkeymasterdevice','libkeymaster3device'):
            part=bp.split('name: "'+module+'"',1)[1].split('srcs: [',1)[1].split(']',1)[0]
            deps=['libpuresoftkeymasterdevice.so'] if module=='libkeymaster3device' else []
            build(module+'.so',[keymaster/name for name in re.findall(r'"(.*?)"',part)],deps)
        km=h/'hardware_interfaces__keymaster/3.0/default'
        build('android.hardware.keymaster@3.0-impl.so',[km/'KeymasterDevice.cpp'],
              ['libkeymaster3device.so','libpuresoftkeymasterdevice.so'])
        build('zx9-keymaster-service',[km/'service.cpp'])
        for item in built:shutil.copy2(work/item['output'],output/item['output'])
    version=tool_version([str(a.cxx), '--version'], env=env)
    write_optional((output/'build.lock.json'), json.dumps({'compiler':str(a.cxx),'version':version,'android_source':str(h),
        'generated_headers':list(map(str,generated)),'android_libraries':list(map(str,libraries)),'targets':built},indent=2)+'\n',encoding='utf-8')
    print('Built',len(built),'Android services in',output)


if __name__=='__main__':main()
