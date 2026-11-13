#!/usr/bin/env python3
"""Integrate current WalkDroid Java and invoke a caller-selected Android build.

This modifies the supplied Android source checkout. The caller chooses the
product, targets, output directory and toolchain. It performs no device I/O.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from apply_walkdroid_framework_source import integrate
from apply_walkdroid_launcher_source import integrate as integrate_launcher
from apply_walkdroid_policy_compat import integrate as integrate_policy
from apply_walkdroid_storage_source import integrate as integrate_storage
from apply_walkdroid_ui_source import integrate as integrate_ui
from apply_walkdroid_identity_source import integrate as integrate_identity
from apply_walkdroid_legacy_graphics import integrate as integrate_legacy_graphics
from apply_walkdroid_slice_compat import integrate as integrate_slice_compat


def default_jobs():
    try:
        cpus = len(os.sched_getaffinity(0))
    except (AttributeError, OSError):
        cpus = os.cpu_count() or 1
    return max(1, min(cpus, 24))


def main(default_targets=None, default_product='aosp_arm-userdebug'):
    default_targets=list(default_targets) if default_targets is not None else ['services']
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--platform-root',type=Path,required=True)
    ap.add_argument('--product',default=default_product,help='Android product (default: '+default_product+')')
    ap.add_argument('--build-label', help='WalkDroid build label; default: current ramdisk/sony/walkdroid-build')
    ap.add_argument('--fingerprint', help='optional caller-provided Android build fingerprint')
    ap.add_argument('--target',action='append',help='Android build target; repeat as needed (default: '+', '.join(default_targets)+')')
    ap.add_argument('--jobs',type=int,default=default_jobs(),
                    help='parallel Android build jobs (default: available CPUs, up to 24)')
    ap.add_argument('--out-dir',type=Path,help='Android output directory; default: a new directory in the platform checkout')
    ap.add_argument('--reuse-compiler-cache',action='store_true',
                    help='allow compiler and remote-execution cache settings from the environment')
    ap.add_argument('--python-bin',type=Path,help='Directory containing the Python interpreter for Android build scripts')
    ap.add_argument('--java-home',type=Path,help='JDK location passed to the Android build environment')
    ap.add_argument('--host-lib-dir',type=Path,action='append',default=[],help='Additional host library directory; repeat as needed')
    ap.add_argument('--llvm-prebuilts-base',type=Path,help='Clang host base passed to the Android build')
    ap.add_argument('--llvm-prebuilts-version',help='Clang directory name passed to the Android build')
    ap.add_argument('--log',type=Path,help='Save build output to this file instead of the terminal')
    ap.add_argument('--apply-policy-compat',action='store_true',help='Integrate the legacy-kernel policy transformation')
    ap.add_argument('--policy-backup-dir',type=Path,help='Backup directory for the original policy build definition')
    args=ap.parse_args();platform=args.platform_root.resolve()
    label=args.build_label or (Path(__file__).resolve().parents[1]/'system/zx300a/ramdisk/sony/walkdroid-build').read_text(encoding='utf-8').strip()
    walkdroid_product=args.product.startswith('walkdroid_zx300a-')
    legacy_graphics=integrate_legacy_graphics(platform) if walkdroid_product else None
    if args.out_dir is None:
        output=Path(tempfile.mkdtemp(prefix='out-walkdroid-',dir=platform)).resolve()
    else:
        output=args.out_dir.resolve();output.mkdir(parents=True,exist_ok=True)
    print('Android build output:',output,flush=True)
    targets=args.target or default_targets
    integration=integrate(platform)
    launcher_integration=integrate_launcher(platform)
    storage_integration=integrate_storage(platform)
    ui_integration=integrate_ui(platform)
    integrate_slice_compat(platform)
    identity_integration=integrate_identity(platform)
    policy_integration=integrate_policy(platform,args.policy_backup_dir) if args.apply_policy_compat else None
    env=os.environ.copy();env['OUT_DIR']=str(output)
    env['WALKDROID_BUILD_LABEL']=label
    if not args.reuse_compiler_cache:
        env.update({'USE_CCACHE':'false','CCACHE_DISABLE':'1','USE_GOMA':'false','USE_RBE':'false'})
    if args.python_bin:
        env['PATH']=str(args.python_bin.resolve())+os.pathsep+env.get('PATH','')
    if args.java_home:
        env['JAVA_HOME']=str(args.java_home.resolve())
    if args.host_lib_dir:
        env['LD_LIBRARY_PATH']=os.pathsep.join([*[str(path.resolve()) for path in args.host_lib_dir],env.get('LD_LIBRARY_PATH','')])
    if args.llvm_prebuilts_base:
        env['LLVM_PREBUILTS_BASE']=str(args.llvm_prebuilts_base.resolve())
    if args.llvm_prebuilts_version is not None:
        env['LLVM_PREBUILTS_VERSION']=args.llvm_prebuilts_version
    # Positional parameters avoid interpolating caller-controlled shell text.
    script='''set -e
source "$1/build/envsetup.sh"
shift
walkdroid_environment="$1"
shift
lunch "$1"
shift
walkdroid_jobs="$1"
shift
walkdroid_label="$1"
shift
walkdroid_requested_fingerprint="$1"
shift
walkdroid_product="$1"
shift
{
    command -v python || :
    python --version 2>&1 || :
    command -v java || :
    java -version 2>&1 || :
} > "$walkdroid_environment"
if [ "$walkdroid_product" = 1 ]; then
    if [ -n "$walkdroid_requested_fingerprint" ]; then
        m -j"$walkdroid_jobs" "BUILD_NUMBER=WalkDroid.$walkdroid_label.ZX300A" \
            "BUILD_FINGERPRINT=$walkdroid_requested_fingerprint" "$@"
    else
        m -j"$walkdroid_jobs" "BUILD_NUMBER=WalkDroid.$walkdroid_label.ZX300A" "$@"
    fi
else
    m -j"$walkdroid_jobs" "$@"
fi
'''
    with tempfile.TemporaryDirectory(prefix='walkdroid-platform-build-') as temp:
        launcher=Path(temp)/'build.sh';launcher.write_text(script,encoding='utf-8')
        environment_report=Path(temp)/'environment.txt'
        command=['bash',str(launcher),str(platform),str(environment_report),args.product,str(args.jobs),label,args.fingerprint or '',
                 '1' if walkdroid_product else '0',*targets]
        if args.log:
            log=args.log.resolve();log.parent.mkdir(parents=True,exist_ok=True)
            print('Android build log:',log,flush=True)
            with log.open('w',encoding='utf-8') as stream:
                result=subprocess.run(command,cwd=platform,env=env,stdout=stream,stderr=subprocess.STDOUT,check=False)
        else:
            result=subprocess.run(command,cwd=platform,env=env,check=False)
        build_environment=environment_report.read_text(encoding='utf-8',errors='replace') if environment_report.exists() else None
    def version(command):
        try:
            probe=subprocess.run(command,cwd=platform,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                                 encoding='utf-8',errors='replace',check=False)
            return probe.stdout.splitlines()[0] if probe.returncode==0 and probe.stdout else None
        except OSError:
            return None
    lock={'platform_root':str(platform),'output':str(output),'product':args.product,'targets':targets,'build_label':label,
          'requested_fingerprint':args.fingerprint,
          'output_selection':'provided' if args.out_dir is not None else 'fresh',
          'compiler_cache_reuse_requested':args.reuse_compiler_cache,
          'compiler_cache_environment':{key:env.get(key) for key in ('USE_CCACHE','CCACHE_DISABLE','USE_GOMA','USE_RBE')},
          'jobs':args.jobs,'source_integration':integration,'launcher_integration':launcher_integration,'storage_integration':storage_integration,'ui_integration':ui_integration,'identity_integration':identity_integration,'legacy_graphics_integration':legacy_graphics,
          'policy_integration':policy_integration,'build_exit_code':result.returncode,
          'python_bin':str(args.python_bin.resolve()) if args.python_bin else None,
          'host_python_version':version(['python','--version']),
          'java_home':str(args.java_home.resolve()) if args.java_home else None,
          'host_java_version':version(['java','-version']),
          'host_lib_dirs':[str(path.resolve()) for path in args.host_lib_dir],
          'llvm_prebuilts_environment':{key:env.get(key) for key in ('LLVM_PREBUILTS_BASE','LLVM_PREBUILTS_VERSION')},
          'build_environment':build_environment,
          'framework_source_revision':version(['git','-C',str(platform/'frameworks/base'),'rev-parse','HEAD'])}
    try:
        (output/'walkdroid-platform.build.lock.json').write_text(json.dumps(lock,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    except OSError as exc:
        print(f'Could not write the build record: {exc}', file=sys.stderr, flush=True)
    if result.returncode:raise SystemExit(result.returncode)
    print('Android source build completed:',', '.join(targets))


if __name__=='__main__':main()
