"""Build the WalkDroid system-server integration from the current sources.

A caller supplies the services.jar to patch.  The tool decompiles that input on
each run, builds fresh helper classes, inserts the WalkDroid hooks and writes a
new jar.  It never accesses a device and does not require a specific hash.
"""
from __future__ import annotations
import argparse
from walkdroid_build_metadata import write_optional
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def run(args):
    subprocess.run([str(x) for x in args], check=True)


def insert_in_method(path: Path, signature: str, anchor: str, addition: str, before: bool = False) -> str:
    if not path.is_file():
        return 'class not found'
    text = path.read_text(encoding='utf-8')
    match = re.search(r'(?ms)^' + re.escape(signature) + r'\n.*?^\.end method', text)
    if match is None:
        return 'method not found'
    method = match.group(0)
    if addition in method:
        return 'already present'
    if anchor not in method:
        return 'insertion point not found'
    method = method.replace(anchor, addition + anchor if before else anchor + addition, 1)
    path.write_text(text[:match.start()] + method + text[match.end():], encoding='utf-8', newline='\n')
    return 'inserted'


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--base-services', type=Path, required=True, help='services.jar from the chosen base system')
    ap.add_argument('--sdk-dir', type=Path, default=ROOT/'tools/android-sdk-28')
    ap.add_argument('--apktool', type=Path, default=ROOT/'tools/apktool_2.11.1.jar')
    ap.add_argument('--output', type=Path, default=ROOT/'build/services-walkdroid.jar')
    args = ap.parse_args()
    src = ROOT/'system/zx300a'
    sources = sorted((src/'jack-framework').glob('*.java')) + sorted((src/'app-exclusive').glob('*.java'))
    sources += [src/'sony-settings'/n for n in ('SonyAudioStatePolicy.java','SonyAudioControlPolicy.java','SonyDseeState.java')]
    jar, d8 = args.sdk_dir/'android.jar', args.sdk_dir/'d8.jar'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='walkdroid-framework-',dir=args.output.parent) as temporary:
        work = Path(temporary)
        classes, dex = work/'classes', work/'dex'
        classes.mkdir(); dex.mkdir()
        run(['javac','-encoding','UTF-8','-parameters','--release','8','-classpath',jar,'-d',classes,*sources])
        compiled = sorted((classes/'com/android/server').glob('WalkDroidJack*.class')) + sorted((classes/'org').rglob('*.class'))
        run(['java','-cp',d8,'com.android.tools.r8.D8','--min-api','28','--lib',jar,'--output',dex,*compiled])
        helper = work/'helper.jar'
        with zipfile.ZipFile(helper,'w',compression=zipfile.ZIP_DEFLATED) as archive:
            archive.write(dex/'classes.dex','classes.dex')
        run(['java','-jar',args.apktool,'d','-r','-o',work/'helper-smali',helper])
        run(['java','-jar',args.apktool,'d','-r','-o',work/'base-smali',args.base_services])
        base = work/'base-smali'
        settings = base/'apktool.yml'
        text = settings.read_text(encoding='utf-8')
        settings.write_text(text.replace('minSdkVersion: 29','minSdkVersion: 28'),encoding='utf-8',newline='\n')
        helper_smali=work/'helper-smali/smali'
        for entry in helper_smali.rglob('*.smali'):
            dest=base/'smali'/entry.relative_to(helper_smali)
            dest.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(entry,dest)
        hooks={}
        hooks['wired_accessory']=insert_in_method(base/'smali/com/android/server/WiredAccessoryManager.smali',
            '.method private onSystemReady()V','    return-void',
            '    invoke-static {p0}, Lcom/android/server/WalkDroidJackObserver;->start(Ljava/lang/Object;)V\n\n',before=True)
        hooks['local_volume_default']=insert_in_method(base/'smali/com/android/server/media/MediaSessionService$SessionManagerImpl.smali',
            '.method private dispatchAdjustVolumeLocked(Ljava/lang/String;IIZIII)V', '    :cond_5\n',
            '    invoke-static {v1, v6, v7}, Lorg/walkdroid/audio/exclusive/SonyVolumeBridge;->adjust(III)Z\n\n    move-result v4\n\n    if-nez v4, :goto_3\n\n')
        hooks['local_volume_session']=insert_in_method(base/'smali/com/android/server/media/MediaSessionRecord.smali',
            '.method public adjustVolume(Ljava/lang/String;IILandroid/media/session/ISessionControllerCallback;ZIIZ)V', '    move-result v1\n',
            '    invoke-static {v1, v8, v9}, Lorg/walkdroid/audio/exclusive/SonyVolumeBridge;->adjust(III)Z\n\n    move-result v2\n\n    if-nez v2, :goto_3\n\n')
        run(['java','-jar',args.apktool,'b',base,'-o',work/'rebuilt.jar'])
        with zipfile.ZipFile(work/'rebuilt.jar') as patched:
            new_dex=patched.read('classes.dex')
        with zipfile.ZipFile(args.base_services) as original,zipfile.ZipFile(args.output,'w') as result:
            for info in original.infolist():
                result.writestr(info,new_dex if info.filename=='classes.dex' else original.read(info.filename))
    write_optional((args.output.parent/'framework.build.lock.json'), json.dumps({
        'base_services':str(args.base_services), 'sdk_dir':str(args.sdk_dir), 'apktool':str(args.apktool),
        'sources':[str(p.relative_to(ROOT)) for p in sources], 'hooks':hooks, 'output':str(args.output)},indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    print(f'Built {args.output}')


if __name__ == '__main__':
    main()
