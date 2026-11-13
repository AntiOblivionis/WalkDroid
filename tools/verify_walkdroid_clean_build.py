#!/usr/bin/env python3
"""Rebuild the currently supported components from an isolated source copy.

Runs on Linux/WSL. External Android/platform/toolchain inputs are supplied by
the builder. This does not assemble a complete operating system or use devices.
"""
from __future__ import annotations
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import threading

from walkdroid_parallel_build import default_jobs
from walkdroid_build_metadata import write_optional

ROOT=Path(__file__).resolve().parents[1]


def default_component_jobs():
    try:
        cpus = len(os.sched_getaffinity(0))
    except (AttributeError, OSError):
        cpus = os.cpu_count() or 1
    return max(1, min(cpus, 7))


def execute_plans(plans, output, component_jobs):
    """Run independent component builds and retain each completed result."""
    stopped=threading.Event();records=[]
    order={plan['name']:index for index,plan in enumerate(plans)}

    def run_one(plan):
        if stopped.is_set():return None
        print('building',plan['name'],flush=True)
        try:
            with plan['log'].open('w',encoding='utf-8') as stream:
                result=subprocess.run(plan['command'],cwd=plan['cwd'],stdout=stream,stderr=subprocess.STDOUT)
        except BaseException:
            stopped.set()
            raise
        record={'name':plan['name'],'status':result.returncode,
                'log':str(plan['log']),'command':plan['command']}
        if result.returncode:stopped.set()
        return record

    with ThreadPoolExecutor(max_workers=component_jobs) as pool:
        futures=[pool.submit(run_one,plan) for plan in plans]
        for future in as_completed(futures):
            record=future.result()
            if record is None:continue
            records.append(record);records.sort(key=lambda item:order[item['name']])
            write_optional((output/'verification.json'),json.dumps(records,indent=2)+'\n',encoding='utf-8')
    return records


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--inputs-root',type=Path,default=ROOT,
                    help='location of external toolchain, platform sources and headers')
    ap.add_argument('--sony-libs',type=Path,help='Sony runtime inputs when building Sony components')
    ap.add_argument('--kernel-source',type=Path,help='caller-provided Sony kernel source archive or checkout')
    ap.add_argument('--vendor-display-dir',type=Path,help='caller-provided mtkfb.h and mtkfb_info.h directory')
    ap.add_argument('--vendor-display-relative-dir',default='drivers/misc/mediatek/videox/mt8590',
                    help='display header directory inside the selected kernel source')
    ap.add_argument('--policy-include',type=Path,help='optional AudioPolicyInterface header directory')
    ap.add_argument('--policy-libs',type=Path,help='platform libraries for audio policy')
    ap.add_argument('--base-policy-service',type=Path)
    ap.add_argument('--patchelf',type=Path)
    ap.add_argument('--cc',type=Path,help='C cross compiler')
    ap.add_argument('--cxx',type=Path,help='C++ cross compiler')
    ap.add_argument('--binutils-dir',type=Path)
    ap.add_argument('--host-lib-dir',type=Path)
    ap.add_argument('--gcc-include',type=Path)
    ap.add_argument('--android-headers',type=Path,help='Android source/header view')
    ap.add_argument('--android-crt',type=Path)
    ap.add_argument('--native-libs',type=Path)
    ap.add_argument('--radio-libs',type=Path)
    ap.add_argument('--system-libs',type=Path)
    ap.add_argument('--glibc-include',type=Path)
    ap.add_argument('--generated-include',type=Path,action='append',help='Generated header directory; repeat as needed')
    ap.add_argument('--generated-root',type=Path,action='append',default=[],help='Android output tree containing generated include roots')
    ap.add_argument('--platform-libs',type=Path,action='append',help='Platform library directory; repeat as needed')
    ap.add_argument('--kernel-cross-prefix',help='Kernel cross compiler prefix')
    ap.add_argument('--kernel-ld',type=Path)
    ap.add_argument('--ion-heap-mask',default='1024')
    ap.add_argument('--output',type=Path)
    ap.add_argument('--jobs',type=int,default=default_jobs(24),
                    help='parallel jobs within the optional kernel build (default: available CPUs, up to 24)')
    ap.add_argument('--component-jobs',type=int,default=default_component_jobs(),
                    help='independent component groups to build concurrently (default: available CPUs, up to 7)')
    ap.add_argument('--native-jobs',type=int,help='parallel native outputs within the native component group')
    ap.add_argument('--sony-jobs',type=int,help='parallel outputs within the Sony runtime component group')
    ap.add_argument('--only',action='append',choices=('native','sony-runtime','audiohal','radio','graphics','audio-services','policy','kernel'),
                    help='build only the named component; may be repeated')
    a=ap.parse_args()
    inputs=a.inputs_root.resolve()
    output=(a.output or ROOT/'build/clean-verification'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')).resolve()
    source=output/'source';(source/'system').mkdir(parents=True)
    shutil.copytree(ROOT/'system/zx300a',source/'system/zx300a')
    (source/'tools').mkdir()
    scripts=['build_walkdroid_native.py','build_walkdroid_sony_runtime.py','build_walkdroid_audiohal.py',
             'build_walkdroid_radio.py','build_walkdroid_graphics.py','build_walkdroid_policy.py',
             'generate_sony_app_policy_forwarders.py','build_walkdroid_kernel.py',
             'build_walkdroid_boot.py','stage_walkdroid_boot.py','stage_walkdroid_runtime.py','build_walkdroid_data_image.py',
             'build_walkdroid_system_image.py','cpio_newc.py','build_sony_audio_settings.py',
             'build_walkdroid_framework.py','build_walkdroid_audio_services.py','walkdroid_generated_headers.py',
             'walkdroid_parallel_build.py','walkdroid_build_metadata.py']
    for script in scripts:shutil.copy2(ROOT/'tools'/script,source/'tools'/script)
    (output/'results').mkdir()
    resolved=lambda value:value.resolve() if value is not None else None
    tools=inputs/'tools';headers=resolved(a.android_headers)
    compiler=resolved(a.binutils_dir) or tools/'cross-root/usr/bin';hostlib=resolved(a.host_lib_dir) or tools/'cross-root/usr/lib/x86_64-linux-gnu'
    gccinclude=resolved(a.gcc_include) or tools/'cross-root/usr/lib/gcc-cross/arm-linux-gnueabihf/15/include'
    cc=resolved(a.cc) or compiler/'arm-linux-gnueabihf-gcc-15';cxx=resolved(a.cxx) or compiler/'arm-linux-gnueabihf-g++-15';crt=resolved(a.android_crt)
    native_libs=resolved(a.native_libs)
    radio_libs=resolved(a.radio_libs) or native_libs
    system_libs=resolved(a.system_libs) or native_libs
    glibc=resolved(a.glibc_include) or tools/'sony-glibc223-headers/usr/arm-linux-gnueabihf/include'
    generated=[resolved(p) for p in (a.generated_include or [])]
    generated_roots=[x for path in a.generated_root for x in ('--generated-root',resolved(path))]
    platform_libs=[resolved(p) for p in a.platform_libs] if a.platform_libs else list(dict.fromkeys(p for p in (native_libs,system_libs) if p is not None))
    shared=['--binutils-dir',str(compiler),'--host-lib-dir',str(hostlib),'--gcc-include',str(gccinclude)]
    cpp=['--cxx',str(cxx),*shared]
    plans=[]

    def option(name,value):
        return [name,value] if value is not None else []

    def options(name,values):
        return [x for value in values if value is not None for x in (name,value)]

    def run(name,script,args):
        if a.only and name not in a.only:return
        log=output/(name+'.log')
        command=[sys.executable,str(source/'tools'/script),*map(str,args),'--output',str(output/'results'/name)]
        plans.append({'name':name,'log':log,'command':command,'cwd':source})

    run('native','build_walkdroid_native.py',['--cc',cc,*shared,*option('--android-headers',headers),
                                             *option('--android-libs',native_libs),*option('--android-crt',crt),
                                             *option('--jobs',a.native_jobs)])
    if not a.only or 'sony-runtime' in a.only:
        if not a.sony_libs:ap.error('--sony-libs is needed for the Sony runtime build')
        run('sony-runtime','build_walkdroid_sony_runtime.py',['--cc',cc,*shared,'--glibc-include',glibc,
                                                             '--sony-libs',a.sony_libs.resolve(),
                                                             *option('--jobs',a.sony_jobs)])
    run('audiohal','build_walkdroid_audiohal.py',['--cc',cxx,*shared,*option('--android-source',headers),*option('--android-crt',crt),
        *options('--generated-include',generated),*generated_roots,
        *options('--android-libs',(native_libs,system_libs))])
    run('radio','build_walkdroid_radio.py',['--cc',cc,*cpp,*option('--android-headers',headers),*option('--android-crt',crt),
        *option('--android-libs',radio_libs),*[x for path in generated for x in ('--generated-include',path)],*generated_roots])
    run('graphics','build_walkdroid_graphics.py',['--cc',cc,*cpp,*option('--android-source',headers),*option('--android-crt',crt),
        *options('--generated-include',generated),*generated_roots,
        *options('--android-libs',platform_libs),*option('--vendor-display-dir',resolved(a.vendor_display_dir)),
        *option('--kernel-source',resolved(a.kernel_source)),'--vendor-display-relative-dir',a.vendor_display_relative_dir,
        '--ion-heap-mask',a.ion_heap_mask])
    run('audio-services','build_walkdroid_audio_services.py',['--cc',cc,*cpp,*option('--android-source',headers),*option('--android-crt',crt),
        *options('--generated-include',generated),*generated_roots,
        *options('--android-libs',(native_libs,system_libs))])
    if (a.policy_include and a.policy_libs) or (a.only and 'policy' in a.only):
        args=[*option('--policy-include',resolved(a.policy_include)),*option('--android-libs',resolved(a.policy_libs)),
              *cpp,*option('--android-headers',headers),*option('--android-crt',crt),
              *option('--base-policy-service',resolved(a.base_policy_service)),*option('--patchelf',resolved(a.patchelf))]
        run('policy','build_walkdroid_policy.py',args)
    elif not a.only:
        print('Skipped policy: supply --policy-include and --policy-libs to build it',flush=True)
    if a.kernel_source or (a.only and 'kernel' in a.only):
        source_option='--source-tree' if a.kernel_source.is_dir() else '--source-archive'
        run('kernel','build_walkdroid_kernel.py',[*option(source_option,resolved(a.kernel_source)),
            '--cross-prefix',a.kernel_cross_prefix or str(tools/'gcc48/bin/arm-eabi-'),
            '--ld',resolved(a.kernel_ld) or tools/'gcc48/bin/arm-eabi-ld.bfd','--jobs',a.jobs])
    elif not a.only:
        print('Skipped kernel: supply --kernel-source to build it',flush=True)
    records=execute_plans(plans,output,a.component_jobs)
    failures=[record for record in records if record['status']]
    for failure in failures:
        print(Path(failure['log']).read_text(encoding='utf-8',errors='replace')[-7000:],file=sys.stderr)
    if failures:raise SystemExit(failures[0]['status'])
    print('Clean source-copy builds completed:',', '.join(record['name'] for record in records))
    print('Results:',output)


if __name__=='__main__':main()
