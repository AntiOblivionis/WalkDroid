#!/usr/bin/env python3
"""Stage an experimental ZX300A product and current settings-app sources.

The selected Android source tree is modified in place. Each refresh selects a
new app-input directory so old source/resources do not enter the generated
Android.mk; it does not erase any previous or caller-owned files.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re
import shutil
import uuid

ROOT=Path(__file__).resolve().parents[1]


def stage(platform: Path, bootanimation: Path | None = None):
    source=ROOT/'system/zx300a';target=platform/'device/sony/zx300a'
    target.mkdir(parents=True,exist_ok=True)
    for name in ('AndroidProducts.mk','BoardConfig.mk','walkdroid_zx300a.mk','strip_policy_rules.py','no-webview.xml','empty-egl-target'):
        shutil.copyfile(source/'android/device'/name,target/name)
    shutil.copyfile(source/'ramdisk/sony/walkdroid-build',target/'walkdroid-build')
    app=target/'app';app.mkdir(exist_ok=True)
    tag=uuid.uuid4().hex;inputs=app/'inputs'/tag;java=inputs/'src';java.mkdir(parents=True)
    overlay=target/'overlay-inputs'/tag
    shutil.copytree(source/'android/device/overlay',overlay)
    (target/'walkdroid_overlay.mk').write_text(
        f'WALKDROID_DEVICE_OVERLAY := device/sony/zx300a/overlay-inputs/{tag}\n',encoding='utf-8')
    sources=sorted((source/'sony-settings').glob('*.java'))
    sources += [source/'app-exclusive'/name for name in ('AppSelectorProtocol.java','BtCatalogProtocol.java','BtLinkProtocol.java')]
    for path in sources:shutil.copyfile(path,java/path.name)
    shutil.copytree(source/'sony-settings/res',inputs/'res')
    shutil.copyfile(source/'sony-settings/AndroidManifest.xml',inputs/'AndroidManifest.xml')
    names=' \\\n    '.join(f'inputs/{tag}/src/{path.name}' for path in sources)
    (app/'Android.mk').write_text(f'''LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE_TAGS := optional
LOCAL_PACKAGE_NAME := WalkDroidSonyAudio
# This player does not need persistent phone/NFC apps, CTS shim packages or
# the platform's precompiled WebView for its current audio and storage flows.
LOCAL_OVERRIDES_PACKAGES := TeleService NfcNci Tag CtsShimPrebuilt CtsShimPrivPrebuilt webview EasterEgg
LOCAL_SRC_FILES := {names}
LOCAL_RESOURCE_DIR := $(LOCAL_PATH)/inputs/{tag}/res
LOCAL_MANIFEST_FILE := inputs/{tag}/AndroidManifest.xml
LOCAL_PRIVATE_PLATFORM_APIS := true
LOCAL_PROGUARD_ENABLED := disabled
LOCAL_CERTIFICATE := $(if $(WALKDROID_SETTINGS_CERTIFICATE),$(WALKDROID_SETTINGS_CERTIFICATE),platform)
include $(BUILD_PACKAGE)
''',encoding='utf-8')
    # The selected platform supplies the GPL exFAT implementation; compile its
    # FUSE frontend with the same source checkout's libraries, not an old binary.
    exfat=target/'exfat';exfat_input=exfat/'inputs'/tag
    exfat_input.mkdir(parents=True,exist_ok=True)
    frontend=(platform/'external/exfat/fuse/main.c').read_text(encoding='utf-8')
    frontend=frontend.replace('#if !defined(FUSE_VERSION) || (FUSE_VERSION < 26)\n\t#error FUSE 2.6 or later is required\n#endif\n','')
    adapter=(source/'android/exfat/fuse_lite_main.c.inc').read_text(encoding='utf-8')
    frontend=re.sub(r'static int fuse_exfat_main\(char\* mount_options, char\* mount_point\)\n\{.*?\n\}',
                    lambda _:adapter.rstrip(),frontend,count=1,flags=re.S)
    (exfat_input/'main.c').write_text(frontend,encoding='utf-8',newline='\n')
    (exfat/'Android.bp').write_text(f'''cc_binary {{
    name: "mount.exfat-fuse",
    srcs: ["inputs/{tag}/main.c"],
    defaults: ["exfat_defaults"],
    static_libs: ["libexfat", "libfuse-lite"],
}}
''',encoding='utf-8')
    animation_make=target/'walkdroid_bootanimation.mk'
    animation_input=None
    if bootanimation is not None:
        animation_input=target/'bootanimation/inputs'/tag/'bootanimation.zip'
        animation_input.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(bootanimation,animation_input)
        relative=f'device/sony/zx300a/bootanimation/inputs/{tag}/bootanimation.zip'
        animation_make.write_text(f'TARGET_BOOTANIMATION := {relative}\n',encoding='utf-8')
    else:
        animation_make.write_text('# No custom boot animation selected.\n',encoding='utf-8')
    return {'device_tree':str(target),'app_input':str(inputs),'overlay_input':str(overlay),'java_sources':len(sources),
            'exfat_input':str(exfat_input),
            'bootanimation_input':str(animation_input) if animation_input else None}


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--platform-root',type=Path,required=True)
    ap.add_argument('--bootanimation',type=Path,help='optional caller-selected Android bootanimation ZIP')
    ap.add_argument('--report',type=Path)
    args=ap.parse_args();data=json.dumps(stage(args.platform_root,args.bootanimation),indent=2,ensure_ascii=False)+'\n'
    if args.report:args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(data,encoding='utf-8')
    print(data,end='')


if __name__=='__main__':main()
