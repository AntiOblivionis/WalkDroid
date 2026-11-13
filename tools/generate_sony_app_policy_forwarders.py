"""Generate AudioPolicy forwarding methods from the selected platform header."""
from __future__ import annotations
import argparse
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]
CUSTOM_POLICY={'setDeviceConnectionState','setForceUse','getOutputForAttr','startOutput','stopOutput','releaseOutput','initCheck','dump','registerPolicyMixes','unregisterPolicyMixes','registerEffect','getOutputForEffect','setMasterMono','createAudioPatch','releaseAudioPatch'}
CUSTOM_CLIENT={'openOutput','openDuplicateOutput','closeOutput','createAudioPatch','releaseAudioPatch','setParameters','setStreamVolume'}

def methods(header,classname):
    text=header.split('class '+classname,1)[1].split('\n};',1)[0]
    text=re.sub(r'/\*.*?\*/|//[^\n]*','',text,flags=re.S)
    return [dict(result=m[0].strip(),name=m[1],parameters=' '.join(m[2].split()),const=bool(m[3]))
            for m in re.findall(r'virtual\s+([\w:<>]+)\s+(\w+)\s*\((.*?)\)\s*(const)?\s*(?:=\s*0)?\s*;',text,flags=re.S)]

def declarations(header,which,skip,target):
    rows=['/* Generated from the selected platform AudioPolicyInterface.h. */']
    for method in methods(header,which):
        if method['name'] in skip:continue
        params=re.sub(r'\s*=\s*0\b','',method['parameters']);args=[]
        for param in params.split(',') if params else []:
            match=re.search(r'(\w+)\s*$',param)
            if not match:raise ValueError('Cannot parse parameter: '+param)
            args.append(match[1])
        const=' const' if method['const'] else ''
        rows.append(method['result']+' '+method['name']+'('+params+')'+const+' override {return '+target+'->'+method['name']+'('+','.join(args)+');}')
    return '\n'.join(rows)+'\n'

def generate(header:Path,output_dir:Path):
    raw=header.read_text(encoding='utf-8');output_dir.mkdir(parents=True,exist_ok=True)
    outputs={'sony_audio_app_policy_forward.inc':declarations(raw,'AudioPolicyInterface',CUSTOM_POLICY,'mBase'),
             'sony_audio_app_policy_client_forward.inc':declarations(raw,'AudioPolicyClientInterface',CUSTOM_CLIENT,'mTarget')}
    for name,text in outputs.items():(output_dir/name).write_text(text,encoding='utf-8')
    return list(outputs)

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--header',type=Path,required=True)
    ap.add_argument('--output-dir',type=Path,default=ROOT/'system/zx300a');a=ap.parse_args()
    print('\n'.join(generate(a.header,a.output_dir)))

if __name__=='__main__':main()
