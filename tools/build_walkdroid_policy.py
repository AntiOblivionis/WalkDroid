#!/usr/bin/env python3
"""Build the AudioPolicy wrapper against a caller-supplied platform interface.

Generated forwarding code is derived from the provided header on every build;
no prebuilt WalkDroid policy library or historical snapshot is used.
"""
from __future__ import annotations
import argparse
from walkdroid_build_metadata import write_optional, tool_version
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from generate_sony_app_policy_forwarders import generate

ROOT=Path(__file__).resolve().parents[1]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--policy-include',type=Path,required=True,help='directory containing the chosen AudioPolicyInterface.h')
    ap.add_argument('--android-libs',type=Path,required=True,help='platform shared libraries extracted from the chosen base system')
    ap.add_argument('--cxx',type=Path,default=ROOT/'tools/cross-root/usr/bin/arm-linux-gnueabihf-g++-15')
    ap.add_argument('--binutils-dir',type=Path,default=ROOT/'tools/cross-root/usr/bin')
    ap.add_argument('--host-lib-dir',type=Path,default=ROOT/'tools/cross-root/usr/lib/x86_64-linux-gnu')
    ap.add_argument('--gcc-include',type=Path,default=ROOT/'tools/cross-root/usr/lib/gcc-cross/arm-linux-gnueabihf/15/include')
    ap.add_argument('--android-headers',type=Path,required=True,help='caller-selected Android source/header view')
    ap.add_argument('--android-crt',type=Path,required=True,help='caller-selected Android CRT directory')
    ap.add_argument('--extra-include',type=Path,action='append',default=[])
    ap.add_argument('--base-policy-service',type=Path,help='libaudiopolicyservice.so from the selected base system')
    ap.add_argument('--patchelf',default='patchelf',help='patchelf executable, used when --base-policy-service is supplied')
    ap.add_argument('--output',type=Path)
    a=ap.parse_args()
    if a.output is None:a.output=ROOT/'build/policy'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    a.output.mkdir(parents=True,exist_ok=True);src=ROOT/'system/zx300a';h=a.android_headers
    includes=['external_libcxx__include','bionic__libc_include','bionic__libc_kernel_uapi',
              'bionic__libc_kernel_uapi/asm-arm','bionic__libc_kernel_android_uapi','external_libcxxabi__include',
              'system_core__libutils_include','system_core__libcutils_include','system_core__liblog_include',
              'system_core__libsystem_include','system_core__base/include','frameworks_native__libs_binder_include',
              'frameworks_native__libs_nativebase_include','frameworks_native__libs_nativewindow_include',
              'frameworks_av__include','frameworks_av__media_libaudioclient_include','frameworks_av__media_libmedia/include',
              'system_media__audio_include','system_media__audio_effects_include','system_media__audio_utils_include',
              'frameworks_av__media_libmedia_include']
    sources=['sony_audio_app_policy.cpp','sony_audio_app_policy_adapter.cpp','sony_audio_app_policy_factory.cpp',
             'sony_audio_policy_tickets.cpp','sony_audio_policy_control_engine.cpp',
             'sony_audio_policy_control_binder.cpp','sony_audio_bt_policy_route.cpp']
    env=os.environ.copy();env['LD_LIBRARY_PATH']=str(a.host_lib_dir)+((':'+env['LD_LIBRARY_PATH']) if env.get('LD_LIBRARY_PATH') else '')
    with tempfile.TemporaryDirectory(prefix='walkdroid-policy-') as temp:
        work=Path(temp);bins=work/'bin';bins.mkdir()
        for n in ('as','ld'):(bins/n).symlink_to((a.binutils_dir/('arm-linux-gnueabihf-'+n)).resolve())
        generate(a.policy_include/'AudioPolicyInterface.h',work)
        adapter=work/'sony_audio_app_policy_adapter.cpp';shutil.copyfile(src/adapter.name,adapter)
        paths=[a.policy_include,src,*a.extra_include,*[h/p for p in includes]]
        cmd=[str(a.cxx),'-B'+str(bins)+'/', '-B'+str(a.binutils_dir)+'/', '-mfloat-abi=softfp',
             '-mfpu=neon','-march=armv7-a','-fPIC','-O2','-std=gnu++14','-fno-exceptions','-fno-rtti',
             '-fno-gnu-unique','-fno-stack-protector','-nostdlib','-nostdinc','-nostdinc++',
             '-D__ANDROID__','-D__ANDROID_API__=28','-DBINDER_IPC_32BIT','-Wno-attributes',
             '-Wno-deprecated-declarations','-Wno-template-body','-include','cstring','-include','climits',
             *['-I'+str(p) for p in paths],'-isystem',str(a.gcc_include),'-fvisibility=hidden',
             '-shared','-Wl,--hash-style=sysv,--no-undefined,-soname,libwdpolicyfactory.so',
             str(a.android_crt/'crtbegin_so.o'),*[str(adapter if n==adapter.name else src/n) for n in sources],
             '-L'+str(a.android_libs),'-Wl,-rpath-link='+str(a.android_libs),'-l:libc++.so',
             '-lmedia_helper','-lutils','-lcutils','-llog','-ldl','-lm','-lc','-lbinder','-lgcc',
             '-o',str(work/'libwdpolicyfactory.so')]
        subprocess.run(cmd,env=env,check=True)
        if a.base_policy_service is not None:
            service=work/'service.so'
            shutil.copyfile(a.base_policy_service,service)
            # The runtime mounts the newly built factory at this path.  Let
            # patchelf manage ELF string tables rather than overwriting bytes
            # that could also be referenced by another dynamic entry.
            subprocess.run([a.patchelf,'--replace-needed','libaudiopolicymanager.so',
                            '/data/wdpolicy.so',str(service)],check=True)
            shutil.copy2(service,a.output/'service.so')
        shutil.copy2(work/'libwdpolicyfactory.so',a.output/'libwdpolicyfactory.so')
    version=tool_version([str(a.cxx), '--version'], env=env)
    write_optional((a.output/'build.lock.json'), json.dumps({'compiler':str(a.cxx),'version':version,
        'policy_header':str(a.policy_include/'AudioPolicyInterface.h'),'android_headers':str(h),
        'android_libraries':str(a.android_libs),'android_crt':str(a.android_crt),
        'base_policy_service':str(a.base_policy_service) if a.base_policy_service is not None else None,
        'patchelf':a.patchelf if a.base_policy_service is not None else None,'sources':sources},indent=2)+'\n',encoding='utf-8')
    print(f'Built AudioPolicy wrapper in {a.output}')


if __name__=='__main__':main()
