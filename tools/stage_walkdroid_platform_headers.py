#!/usr/bin/env python3
"""Expose Android platform sources under the names used by component builds.

Links refer to the caller-selected checkout. Missing paths are recorded and
do not prevent use of the remaining sources. Additional mappings may be given
for platform variants and separately supplied sources.
"""
from __future__ import annotations
import argparse
from walkdroid_build_metadata import write_optional
import json
from pathlib import Path

NAMES='''
bionic__libc_include bionic__libc_kernel_android_uapi bionic__libc_kernel_uapi
external_boringssl__src_include external_libcxx__include external_libcxxabi__include
external_minijail__ external_tinyalsa frameworks_av__include
frameworks_av__media_libaudioclient_include frameworks_av__media_libaudiohal
frameworks_av__media_libaudiohal_include frameworks_av__media_libeffects_config_include
frameworks_av__media_libeffects_factory frameworks_av__media_libeffects_factory_include
frameworks_av__media_libmedia frameworks_av__media_libmedia_include frameworks_av__media_libmediautils_include
frameworks_av__media_libstagefright_omx_include frameworks_av__media_libstagefright_xmlparser_include
frameworks_native__libs_binder_include frameworks_native__libs_nativebase_include
frameworks_native__libs_nativewindow_include frameworks_native__libs_ui_include frameworks_native__opengl_include
hardware_interfaces__audio hardware_interfaces__bluetooth hardware_interfaces__configstore
hardware_interfaces__graphics hardware_interfaces__keymaster hardware_interfaces__light
hardware_interfaces__media hardware_interfaces__nfc hardware_libhardware__include
system_core__base system_core__include system_core__init system_core__libcutils_include
system_core__libion system_core__liblog_include system_core__libpackagelistparser_include
system_core__libsync system_core__libsystem_include system_core__libutils_include system_core__sdcard
system_keymaster__ system_keymaster__include system_libfmq__include system_libhidl__base_include
system_libhidl__libhidlmemory_include system_libhidl__transport system_libhidl__transport_include
system_libhwbinder__include system_media__audio_effects_include system_media__audio_include
system_media__audio_utils_include system_nfc__src system_security__keystore system_tools_hidl__ system_vold__
'''.split()


def relative(name):
    return Path(*[part for part in name.replace('__','_').split('_') if part])


def stage(platform: Path, output: Path, extra: list[str]):
    mapping={name:platform/relative(name) for name in NAMES}
    mapping['frameworks_av__media_libmediautils_include']=platform/'frameworks/av/media/utils/include'
    mapping['system_media__audio_effects_include']=platform/'system/media/audio_effects/include'
    mapping['system_media__audio_utils_include']=platform/'system/media/audio_utils/include'
    mapping['aaudio-include']=platform/'frameworks/av/media/libaaudio/include'
    # The Oreo FUSE implementation is a separate source input; newer platform
    # trees may have an sdcard directory with a different implementation.
    mapping['oreo_sdcard']=None
    for item in extra:
        name,source=item.split('=',1)
        mapping[name]=Path(source).resolve()
    output.mkdir(parents=True,exist_ok=False)
    linked={};missing={}
    for name,source in mapping.items():
        if source is None:
            missing[name]='provide an Oreo sdcard source directory with --extra'
        elif source.exists():
            (output/name).symlink_to(source,target_is_directory=source.is_dir())
            linked[name]=str(source)
        else:
            missing[name]=str(source)
    data={'platform_root':str(platform),'linked_sources':linked,'missing_sources':missing}
    write_optional((output/'build.lock.json'), json.dumps(data,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    return data


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform-root',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--extra',action='append',default=[],metavar='NAME=PATH',help='Add or replace an input mapping')
    args=parser.parse_args();data=stage(args.platform_root.resolve(),args.output.resolve(),args.extra)
    print('Linked',len(data['linked_sources']),'platform source paths; unavailable:',len(data['missing_sources']))


if __name__=='__main__':main()
