#!/usr/bin/env python3
"""Copy WalkDroid helpers into an Android source tree and add source hooks.

The platform tree is supplied by the caller and is modified in place. Missing
hook locations are reported rather than stopping other work. This prepares
source integration only; it does not build or deploy an Android system.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re
import shutil

ROOT=Path(__file__).resolve().parents[1]


def method_body(text: str, signature: str):
    match=re.search(signature,text,re.MULTILINE|re.DOTALL)
    if not match:return None
    start=match.end()-1;depth=0;mode='code';escape=False;i=start
    while i<len(text):
        c=text[i];n=text[i+1] if i+1<len(text) else ''
        if mode=='line':
            if c=='\n':mode='code'
        elif mode=='block':
            if c=='*' and n=='/':mode='code';i+=1
        elif mode in ('string','char'):
            if escape:escape=False
            elif c=='\\':escape=True
            elif c==('"' if mode=='string' else "'"):mode='code'
        elif c=='/' and n=='/':mode='line';i+=1
        elif c=='/' and n=='*':mode='block';i+=1
        elif c=='"':mode='string'
        elif c=="'":mode='char'
        elif c=='{':depth+=1
        elif c=='}':
            depth-=1
            if not depth:return start,i
        i+=1
    return None


def hook(path: Path, signature: str, marker: str, anchor: str | None, addition: str):
    if not path.is_file():return 'source file missing'
    text=path.read_text(encoding='utf-8');bounds=method_body(text,signature)
    if not bounds:return 'method not found'
    start,end=bounds;body=text[start:end]
    if marker in body:return 'already present'
    if anchor is None:
        position=text.rfind('\n',start,end)+1
        if text[position:end].strip():position=end;addition='\n'+addition
    else:
        found=re.search(anchor,body,re.MULTILINE)
        if not found:return 'insertion point not found'
        position=start+found.end()
    path.write_text(text[:position]+addition+text[position:],encoding='utf-8',newline='\n')
    return 'inserted'


def integrate(platform: Path):
    src=ROOT/'system/zx300a';java=platform/'frameworks/base/services/core/java'
    sources=sorted((src/'jack-framework').glob('*.java'))+sorted((src/'app-exclusive').glob('*.java'))
    sources += [src/'sony-settings'/name for name in ('SonyAudioStatePolicy.java','SonyAudioControlPolicy.java','SonyDseeState.java')]
    copied=[]
    for source in sources:
        match=re.search(r'^\s*package\s+([\w.]+)\s*;',source.read_text(encoding='utf-8'),re.MULTILINE)
        if not match:continue
        dest=java.joinpath(*match.group(1).split('.'))/source.name
        dest.parent.mkdir(parents=True,exist_ok=True)
        if not dest.exists() or source.read_bytes()!=dest.read_bytes():
            shutil.copyfile(source,dest)
        copied.append(str(dest.relative_to(platform)))
    results={}
    results['wired_accessory']=hook(java/'com/android/server/WiredAccessoryManager.java',
        r'\bprivate\s+void\s+onSystemReady\s*\(\s*\)\s*\{',
        'WalkDroidJackObserver.start(this);',None,
        '        WalkDroidJackObserver.start(this);\n')
    results['local_volume_default']=hook(java/'com/android/server/media/MediaSessionService.java',
        r'\bprivate\s+void\s+dispatchAdjustVolumeLocked\s*\([^)]*\)\s*\{',
        'SonyVolumeBridge.adjust(suggestedStream, direction, flags)',
        r'(?m)^(?=[ \t]*mHandler\.post\(new Runnable\(\))',
        '                if (org.walkdroid.audio.exclusive.SonyVolumeBridge.adjust(suggestedStream, direction, flags)) return;\n')
    results['local_volume_session']=hook(java/'com/android/server/media/MediaSessionRecord.java',
        r'\bpublic\s+void\s+adjustVolume\s*\([^)]*ISessionControllerCallback[^)]*\)\s*\{',
        'SonyVolumeBridge.adjust(stream, direction, flags)',
        r'\bint\s+stream\s*=\s*AudioAttributes\.toLegacyStreamType\s*\(\s*mAudioAttrs\s*\)\s*;[^\S\n]*\n',
        '            if (org.walkdroid.audio.exclusive.SonyVolumeBridge.adjust(stream, direction, flags)) return;\n')
    return {'platform_root':str(platform),'copied_sources':copied,'hooks':results}


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--platform-root',type=Path,required=True,help='Android source checkout to modify in place')
    ap.add_argument('--report',type=Path,help='optional output report')
    args=ap.parse_args();result=integrate(args.platform_root)
    data=json.dumps(result,indent=2,ensure_ascii=False)+'\n'
    if args.report:args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(data,encoding='utf-8')
    print(data,end='')


if __name__=='__main__':main()
