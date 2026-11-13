#!/usr/bin/env python3
"""Build a caller-supplied BusyBox source tree for an offline boot image.

The source is copied into a temporary directory. No device operation occurs.
The default produces a static binary; the caller may provide another config
and toolchain. Build metadata is informational and never controls later builds.
"""
from __future__ import annotations
import argparse
from walkdroid_build_metadata import write_optional
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

from walkdroid_parallel_build import default_jobs

ROOT=Path(__file__).resolve().parents[1]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    source=ap.add_mutually_exclusive_group(required=True)
    source.add_argument('--source-archive',type=Path)
    source.add_argument('--source-tree',type=Path)
    ap.add_argument('--config',type=Path,help='BusyBox .config; otherwise use defconfig')
    ap.add_argument('--use-source-config',action='store_true',help='Use an included .config when present')
    ap.add_argument('--patch',type=Path,action='append',default=[],help='Optional source patch; repeat as needed')
    ap.add_argument('--disable',action='append',default=[],metavar='OPTION',help='Disable a BusyBox config option; repeat as needed')
    ap.add_argument('--enable',action='append',default=[],metavar='OPTION',help='Enable a BusyBox config option; repeat as needed')
    ap.add_argument('--dynamic',action='store_true',help='Build dynamically instead of setting CONFIG_STATIC')
    ap.add_argument('--cc',type=Path,default=ROOT/'tools/cross-root/usr/bin/arm-linux-gnueabihf-gcc-15')
    ap.add_argument('--binutils-dir',type=Path,default=ROOT/'tools/cross-root/usr/bin')
    ap.add_argument('--binutils-prefix',default='arm-linux-gnueabihf-')
    ap.add_argument('--host-lib-dir',type=Path,default=ROOT/'tools/cross-root/usr/lib/x86_64-linux-gnu')
    ap.add_argument('--sysroot',type=Path,default=ROOT/'tools/cross-root')
    ap.add_argument('--cflags',default='',help='Additional C compiler flags')
    ap.add_argument('--jobs',type=int,default=default_jobs(24),
                    help='parallel BusyBox build jobs (default: available CPUs, up to 24)')
    ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy();env['LD_LIBRARY_PATH']=str(args.host_lib_dir.resolve())+(':'+env['LD_LIBRARY_PATH'] if env.get('LD_LIBRARY_PATH') else '')
    log_path=out/'build.log';print('BusyBox build log:',log_path,flush=True)
    with tempfile.TemporaryDirectory(prefix='walkdroid-busybox-') as temp,log_path.open('w',encoding='utf-8') as log:
        work=Path(temp);source_dir=work/'source';source_dir.mkdir()
        if args.source_tree:
            shutil.copytree(args.source_tree.resolve(),source_dir,dirs_exist_ok=True,symlinks=True)
        else:
            subprocess.run(['tar','-xf',str(args.source_archive.resolve()),'-C',str(source_dir)],stdout=log,stderr=subprocess.STDOUT,check=True)
            if not (source_dir/'Makefile').exists() and not (source_dir/'Makefile.native').exists():
                source_dir=next(path for path in source_dir.iterdir() if path.is_dir() and
                                ((path/'Makefile').exists() or (path/'Makefile.native').exists()))
        if not (source_dir/'Makefile').exists() and (source_dir/'Makefile.native').exists():
            shutil.copyfile(source_dir/'Makefile.native',source_dir/'Makefile')
        included_config=work/'included.config'
        if args.use_source_config and (source_dir/'.config').is_file():shutil.copyfile(source_dir/'.config',included_config)
        # Clean only the private copy so source archives containing old build
        # products cannot feed the new object directory.
        subprocess.run(['make','-C',str(source_dir),'distclean'],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
        for patch in args.patch:
            subprocess.run(['patch','-d',str(source_dir),'-p1','-i',str(patch.resolve())],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
        objects=work/'objects';objects.mkdir();bins=work/'bin';bins.mkdir()
        for name in ('as','ld'):
            (bins/name).symlink_to((args.binutils_dir/(args.binutils_prefix+name)).resolve())
        compiler=' '.join([shlex.quote(str(args.cc.resolve())),'-B'+shlex.quote(str(bins))+'/',
                           '--sysroot='+shlex.quote(str(args.sysroot.resolve()))])
        common=['make','-C',str(source_dir),'O='+str(objects),'ARCH=arm',
                'CROSS_COMPILE='+str((args.binutils_dir/args.binutils_prefix).resolve()),
                'CC='+compiler,'EXTRA_CFLAGS='+args.cflags]
        def run(target):
            log.write('\n$ '+shlex.join(common+target)+'\n');log.flush()
            result=subprocess.run(common+target,env=env,stdout=log,stderr=subprocess.STDOUT,input='\n'*8192,text=True,check=False)
            if result.returncode:raise SystemExit(result.returncode)
        if args.config:shutil.copyfile(args.config.resolve(),objects/'.config')
        elif included_config.exists():shutil.copyfile(included_config,objects/'.config')
        else:run(['defconfig'])
        config=objects/'.config';lines=config.read_text(encoding='utf-8').splitlines()
        settings={name.removeprefix('CONFIG_'):False for name in args.disable}
        settings.update({name.removeprefix('CONFIG_'):True for name in args.enable})
        settings['STATIC']=not args.dynamic
        for name,enabled in settings.items():
            key='CONFIG_'+name
            lines=[line for line in lines if not line.startswith(key+'=') and line!='# '+key+' is not set']
            lines.append(key+'=y' if enabled else '# '+key+' is not set')
        config.write_text('\n'.join(lines)+'\n',encoding='utf-8')
        run(['oldconfig'])
        run(['-j'+str(args.jobs)])
        shutil.copy2(objects/'busybox',out/'busybox')
        shutil.copy2(objects/'.config',out/'busybox.config')
    write_optional((out/'build.lock.json'), json.dumps({'source_archive':str(args.source_archive) if args.source_archive else None,
        'source_tree':str(args.source_tree) if args.source_tree else None,
        'config':str(args.config) if args.config else ('source config when present' if args.use_source_config else 'defconfig'),
        'patches':[str(p.resolve()) for p in args.patch],
        'enabled':args.enable,'disabled':args.disable,
        'compiler':str(args.cc.resolve()),'sysroot':str(args.sysroot.resolve()),'cflags':args.cflags,
        'static':not args.dynamic,'jobs':args.jobs},indent=2)+'\n',encoding='utf-8')
    print('Built BusyBox in',out)


if __name__=='__main__':main()
