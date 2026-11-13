#!/usr/bin/env python3
"""Stage rebuilt components for the offline WalkDroid boot-image assembler.

Inputs are optional caller-selected output directories. Their present files are
staged without reading build records; base Android init, firmware, proprietary
assets and runtime/data seeds must be supplied separately.
"""
from __future__ import annotations
import argparse
from walkdroid_build_metadata import write_optional
import json
from pathlib import Path
import shutil


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('native','graphics','audio-services','radio','policy','audiohal','kernel'):
        ap.add_argument('--'+name+'-output',type=Path)
    ap.add_argument('--settings-apk',type=Path)
    ap.add_argument('--build-label',help='optional build label; default: current ramdisk/sony/walkdroid-build')
    ap.add_argument('--fingerprint',help='optional explicit fingerprint, matching the platform build if supplied')
    ap.add_argument('--busybox',type=Path,help='caller-provided initramfs busybox')
    ap.add_argument('--android-init',type=Path,help='caller-provided Android init executable')
    ap.add_argument('--adbd',type=Path,help='caller-provided Android ADB daemon')
    ap.add_argument('--sepolicy',type=Path,help='caller-provided compiled Android SELinux policy')
    ap.add_argument('--policy-contexts',type=Path,
                    help='directory containing the Android platform-generated *_contexts files')
    ap.add_argument('--platform-default-prop',type=Path,
                    help='Android platform-generated root/default.prop')
    ap.add_argument('--mali-lib',type=Path,help='caller-provided Mali runtime library')
    ap.add_argument('--ld-config',type=Path,help='caller-provided Android linker configuration')
    ap.add_argument('--radio-launcher',type=Path,help='caller-provided original radio launcher')
    ap.add_argument('--radio-libs',type=Path,help='caller-provided launcher libraries')
    ap.add_argument('--radio-native-output',type=Path,help='output of prepare_native_radio_runtime.py')
    ap.add_argument('--radio-firmware',type=Path,help='caller-provided radio firmware directory')
    ap.add_argument('--radio-apps',type=Path,help='optional Bluetooth/NFC APK overlay tree')
    ap.add_argument('--dac-assets',type=Path,help='caller-provided DAC initialization table directory')
    ap.add_argument('--gain-assets',type=Path,help='caller-provided gain-profile directory')
    ap.add_argument('--asset',action='append',nargs=2,metavar=('DEST','SOURCE'),default=[],
                    help='copy a caller-selected data file or directory to a ramdisk path; repeat as needed')
    ap.add_argument('--output',type=Path,required=True)
    a=ap.parse_args()
    root=a.output/'root';root.mkdir(parents=True,exist_ok=True);entries={}

    def add(source,dest,executable=False):
        target=root/dest;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
        entries[dest]={'path':dest,'source':'root/'+dest,'mode':0o100755 if executable else 0o100644}

    def add_tree(source,dest,executable_loaders=False):
        if source:
            for path in sorted(source.rglob('*')):
                if path.is_file():
                    add(path,(Path(dest)/path.relative_to(source)).as_posix(),
                        executable_loaders and path.name.startswith('ld-'))

    def outputs(folder):
        return sorted(p.name for p in folder.iterdir() if p.is_file() and p.name!='build.lock.json')

    def shared_library(name):
        return name.endswith('.so') or '.so.' in name

    label=a.build_label or (Path(__file__).resolve().parents[1]/'system/zx300a/ramdisk/sony/walkdroid-build').read_text(encoding='utf-8').strip()
    version_file=root/'sony/walkdroid-build';version_file.parent.mkdir(parents=True,exist_ok=True)
    version_file.write_text(label+'\n',encoding='utf-8')
    entries['sony/walkdroid-build']={'path':'sony/walkdroid-build','source':'root/sony/walkdroid-build','mode':0o100644}
    fingerprint_file=root/'sony/walkdroid-fingerprint'
    fingerprint_file.write_text((a.fingerprint or '')+'\n',encoding='utf-8')
    entries['sony/walkdroid-fingerprint']={'path':'sony/walkdroid-fingerprint','source':'root/sony/walkdroid-fingerprint','mode':0o100644}

    # Empty ramdisks need these mount points before either init is run.
    for dest in ('dev','proc','sys','data','system','sbin',
                 'vendor/sony-audio/rootfs','vendor/sony-audio-ai/rootfs',
                 'vendor/walkdroid-bt-tx-runtime'):
        entries[dest]={'path':dest,'mode':0o040755}
    entries['sbin/ueventd']={'path':'sbin/ueventd','mode':0o120777,'target':'/init'}
    for source,dest,executable in ((a.busybox,'sony/busybox',True),
                                   (a.android_init,'sony/init9',True),
                                   (a.adbd,'sbin/adbd',True),
                                   (a.sepolicy,'sepolicy',False),
                                   (a.platform_default_prop,'sony/platform-default.prop',False),
                                   (a.mali_lib,'sony/libGLES_mali-pie.so',False),
                                   (a.ld_config,'sony/ld.config-zx9.txt',False),
                                   (a.radio_launcher,'sony/radio/native/6620_launcherA',True)):
        if source:add(source,dest,executable)
    add_tree(a.radio_libs,'sony/radio/native/lib',executable_loaders=True)
    for source,dest in ((a.radio_firmware,'sony/radio/firmware'),
                        (a.radio_apps,'sony/radio/apps'),
                        (a.dac_assets,'sony/audio-dac'),
                        (a.gain_assets,'sony/audio-gain/assets')):
        add_tree(source,dest)
    if a.radio_native_output:
        add(a.radio_native_output/'6620_launcherA','sony/radio/native/6620_launcherA',True)
        add_tree(a.radio_native_output/'lib','sony/radio/native/lib',executable_loaders=True)
    for dest,source_name in a.asset:
        source=Path(source_name)
        if source.is_dir():add_tree(source,dest)
        else:add(source,dest)
    if a.policy_contexts:
        for path in sorted(a.policy_contexts.iterdir()):
            if path.is_file() and path.name.endswith('_contexts'):
                add(path,path.name)

    if a.native_output:
        specials={'audio.primary.sony.so':'vendor/lib/hw/audio.primary.default.so',
                  'audio.a2dp.default.so':'vendor/lib/hw/audio.a2dp.default.so',
                  'lights.default.so':'vendor/lib/hw/lights.default.so',
                  'internal-ext4-dm-static':'sony/internal-ext4-dm-static',
                  'sony-audio-dac-init':'sony/audio-dac/sony-audio-dac-init',
                  'sony-audio-gain-select':'sony/audio-gain/sony-audio-gain-select',
                  'sony-audio-gain-tables':'sony/audio-gain/sony-audio-gain-tables',
                  'sony-bt-catalogd':'sony/bt-catalog/sony-bt-catalogd',
                  'sony-bt-linkd':'sony/bt-link/sony-bt-linkd',
                  'sony-bt-link-ctl':'sony/bt-link/sony-bt-link-ctl'}
        for name in outputs(a.native_output):
            shared=shared_library(name)
            dest=specials.get(name,('vendor/lib/' if shared else 'vendor/bin/')+name)
            add(a.native_output/name,dest,not shared)
            if name=='libwalkdroid-sony-bt-catalog.so':add(a.native_output/name,'sony/bt-catalog/'+name)
    if a.graphics_output:
        for name in outputs(a.graphics_output):
            shared=shared_library(name)
            if name in ('gralloc.default.so','android.hardware.graphics.allocator@2.0-impl.so',
                        'android.hardware.graphics.mapper@2.0-impl.so','android.hardware.graphics.composer@2.1-impl.so'):
                dest='vendor/lib/hw/'+name
            elif shared:dest='vendor/lib/'+name
            elif name.startswith('android.hardware.graphics.'):dest='vendor/bin/hw/'+name
            else:dest='sony/'+name
            add(a.graphics_output/name,dest,not shared)
    if a.audio_services_output:
        for name in outputs(a.audio_services_output):
            shared=shared_library(name)
            if shared:
                dest=('vendor/lib/hw/' if name.startswith('android.hardware.') else 'vendor/lib/')+name
            else:
                dest='vendor/bin/hw/'+name
            add(a.audio_services_output/name,dest,not shared)
    if a.radio_output:add(a.radio_output/'zx9-radio-service','sony/radio/zx9-radio-service',True)
    if a.policy_output:
        add(a.policy_output/'libwdpolicyfactory.so','sony/app-policy/factory.so')
        add(a.policy_output/'service.so','sony/app-policy/service.so')
    if a.audiohal_output:
        add(a.audiohal_output/'libaudiohal@2.0.so','sony/bt-output/framework-audiohal.so')
        add(a.audiohal_output/'libaudiohal_deathhandler.so','sony/bt-output/deathhandler.so')
    if a.kernel_output:
        for name in ('wm_key','zx9_aslr','zx9_hwbinder','zx9_zram'):
            add(a.kernel_output/'modules'/(name+'.ko'),'sony/'+name+'.ko')
        carddet = a.kernel_output/'modules/icx_carddet.ko'
        if carddet.is_file():
            add(carddet,'sony/icx_carddet.ko')
        for path in ('drivers/misc/mediatek/common_detect/mtk_wmt_detect.ko',
                     'drivers/misc/mediatek/conn_soc/common/mtk_stp_wmt_soc.ko',
                     'drivers/misc/mediatek/conn_soc/common/mtk_stp_bt_soc.ko'):
            add(a.kernel_output/'modules/kernel'/path,'sony/radio/'+Path(path).name)
    if a.settings_apk:add(a.settings_apk,'vendor/app/WalkDroidSonyAudio/WalkDroidSonyAudio.apk')
    manifest=a.output/'entries.json'
    manifest.write_text(json.dumps(list(entries.values()),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    write_optional((a.output/'stage.lock.json'), 
        json.dumps(vars(a),ensure_ascii=False,indent=2,default=str)+'\n',encoding='utf-8')
    print('staged',len(entries),'entries in',manifest)


if __name__=='__main__':main()
