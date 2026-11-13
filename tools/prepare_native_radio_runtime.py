#!/usr/bin/env python3
"""Stage a supplied radio launcher and its discoverable shared libraries.

The caller supplies the launcher and one or more search directories. No device,
backup image, fixed archive, library hash or particular machine is selected.
Unresolved library names are recorded so another input directory can be supplied.
The resulting files may be proprietary and are not an open-source distribution.
"""
from __future__ import annotations
import argparse
from walkdroid_build_metadata import write_optional
import json
from pathlib import Path
import re
import shutil
import subprocess


def dependencies(path: Path, readelf: str):
    found=set(); errors=[]
    for option in ('-d','-l'):
        result=subprocess.run([readelf,option,str(path)],capture_output=True,text=True)
        if result.returncode:
            errors.append({'file':path.name,'operation':option,'returncode':result.returncode})
            continue
        found.update(re.findall(r'Shared library:\s*\[([^]]+)\]',result.stdout))
        found.update(re.findall(r'Requesting program interpreter:\s*([^\]\s]+)',result.stdout))
    return found,errors


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--launcher',type=Path,required=True)
    ap.add_argument('--lib-dir',type=Path,action='append',default=[],help='library search directory; repeat as needed')
    ap.add_argument('--lib-alias',action='append',default=[],metavar='NAME=FILE',
                    help='map a dependency name to a supplied file; repeat as needed')
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--readelf',default='readelf')
    args=ap.parse_args()
    output=args.output;libs=output/'lib';libs.mkdir(parents=True,exist_ok=True)
    launcher=output/'6620_launcherA'
    if args.launcher.resolve()!=launcher.resolve():shutil.copyfile(args.launcher,launcher)
    launcher.chmod(0o755)
    available={}
    for folder in args.lib_dir:
        for path in sorted(folder.iterdir()):
            if path.is_file():available.setdefault(path.name,path)
    for item in args.lib_alias:
        name,path=item.split('=',1);available[name]=Path(path)
    pending,errors=dependencies(launcher,args.readelf)
    copied={};unresolved=set()
    while pending:
        name=Path(pending.pop()).name
        if name in copied or name in unresolved:continue
        source=available.get(name)
        if source is None:
            unresolved.add(name);continue
        target=libs/name
        if source.resolve()!=target.resolve():shutil.copyfile(source,target)
        target.chmod(0o644);copied[name]=str(source)
        more,issues=dependencies(target,args.readelf);pending.update(more);errors.extend(issues)
    manifest={'launcher':str(args.launcher),'libraries':copied,
              'search_directories':[str(p) for p in args.lib_dir],
              'aliases':args.lib_alias,
              'unresolved':sorted(unresolved),'readelf_errors':errors}
    write_optional((output/'build.lock.json'), json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print('Radio launcher staged:',len(copied),'libraries;',len(unresolved),'unresolved names')


if __name__=='__main__':main()
