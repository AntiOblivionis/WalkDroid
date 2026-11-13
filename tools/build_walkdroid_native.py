#!/usr/bin/env python3
"""Build WalkDroid's native foundation from the current source tree (Linux/WSL).

The Android platform libraries and cross toolchain may be supplied by the
builder.  Each invocation compiles every listed source in a fresh temporary
workspace; no earlier WalkDroid object or library is an input.
"""
from __future__ import annotations
import argparse
from walkdroid_build_metadata import write_optional, tool_version
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from walkdroid_parallel_build import default_jobs, run_compiles

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc', type=Path, default=ROOT/'tools/cross-root/usr/bin/arm-linux-gnueabihf-gcc-15')
    ap.add_argument('--binutils-dir', type=Path, default=ROOT/'tools/cross-root/usr/bin')
    ap.add_argument('--host-lib-dir', type=Path, default=ROOT/'tools/cross-root/usr/lib/x86_64-linux-gnu')
    ap.add_argument('--gcc-include', type=Path, default=ROOT/'tools/cross-root/usr/lib/gcc-cross/arm-linux-gnueabihf/15/include')
    ap.add_argument('--android-headers', type=Path, required=True, help='caller-selected Android source/header view')
    ap.add_argument('--android-libs', type=Path, required=True, help='caller-selected Android platform library directory')
    ap.add_argument('--android-crt', type=Path, required=True, help='caller-selected Android CRT directory')
    ap.add_argument('--jobs', type=int, default=default_jobs(12),
                    help='parallel native outputs (default: available CPUs, up to 12)')
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    if args.output is None:
        args.output=ROOT/'build/native'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    args.output.mkdir(parents=True,exist_ok=True)
    src=ROOT/'system/zx300a'; headers=args.android_headers; crt=args.android_crt
    env=os.environ.copy()
    env['LD_LIBRARY_PATH']=str(args.host_lib_dir)+((':'+env['LD_LIBRARY_PATH']) if env.get('LD_LIBRARY_PATH') else '')
    commands=[]; plans=[]
    with tempfile.TemporaryDirectory(prefix='walkdroid-native-') as temporary:
        work=Path(temporary); tools=work/'bin'; tools.mkdir()
        # Some Windows archive extractors cannot recreate Linux symlinks.  Use
        # the actual cross-binutils executables through fresh local links.
        for name in ('as','ld'):
            (tools/name).symlink_to((args.binutils_dir/('arm-linux-gnueabihf-'+name)).resolve())
        includes=['bionic__libc_include','bionic__libc_kernel_uapi',
                  'bionic__libc_kernel_uapi/asm-arm','bionic__libc_kernel_android_uapi',
                  'hardware_libhardware__include','system_media__audio_include',
                  'system_core__libsystem_include','system_core__libcutils_include',
                  'system_core__libutils_include','system_core__liblog_include',
                  'frameworks_native__libs_nativebase_include','frameworks_native__libs_nativewindow_include',
                  'external_boringssl__src_include','external_tinyalsa/include']
        common=[str(args.cc),'-B'+str(tools)+'/', '-B'+str(args.binutils_dir)+'/',
                '-march=armv7-a','-mfpu=neon','-mfloat-abi=softfp','-std=gnu11','-O2','-fPIC',
                '-fno-stack-protector','-nostdlib','-nostdinc','-D__ANDROID__','-D__ANDROID_API__=28',
                '-Wall','-Wextra','-Wno-attributes','-Wno-misleading-indentation', '-I'+str(src),
                *['-I'+str(headers/p) for p in includes],'-isystem',str(args.gcc_include),
                '-Wl,--hash-style=sysv','-Wl,--no-undefined']
        def compile_one(name, sources, libs=(), executable=False, defines=()):
            output=work/name
            flags=['-pie','-Wl,--dynamic-linker=/system/bin/linker',str(crt/'crtbegin_dynamic.o')] if executable else [
                   '-shared','-Wl,-soname,'+name,str(crt/'crtbegin_so.o')]
            cmd=common+['-D'+x for x in defines]+flags+[str(src/x if isinstance(x,str) else x) for x in sources]+['-L'+str(work),'-L'+str(args.android_libs),
                 '-Wl,-rpath-link='+str(work),'-Wl,-rpath-link='+str(args.android_libs),*['-l:'+x for x in libs],'-lc','-lgcc']
            if executable:cmd.append(str(crt/'crtend_android.o'))
            cmd+=['-o',str(output)]
            commands.append({'output':name,'sources':[str(x) for x in sources],'libraries':list(libs),'defines':list(defines)})
            plans.append({'output':name,'libraries':list(libs),'command':cmd})
        compile_one('libtinyalsa.so',[headers/'external_tinyalsa/mixer.c',headers/'external_tinyalsa/pcm.c'])
        compile_one('libwalkdroid-sony-cpu.so',['sony_audio_cpu.c'])
        compile_one('libwalkdroid-sony-chain-client.so',['sony_audio_chain_client.c'])
        compile_one('libwalkdroid-sony-output-adapter.so',['sony_audio_output_adapter.c'],['libwalkdroid-sony-chain-client.so'])
        compile_one('libwalkdroid-sony-worker-process.so',['sony_audio_worker_process.c'],['libwalkdroid-sony-chain-client.so'])
        compile_one('libwalkdroid-sony-owner.so',['sony_audio_owner.c'])
        compile_one('libsony_ai_process.so',['sony_audio_ai_process.c'],['libwalkdroid-sony-owner.so','libcrypto.so'])
        compile_one('audio.primary.sony.so',[
            'sony_audio_preset_controls.c','sony_audio_sound_presets.c','sony_audio_ai_hal.c',
            'sony_audio_cpu.c','sony_audio_dsd_power_linux.c','sony_audio_dsd_power_platform.c',
            'sony_audio_owner_commands.c','sony_audio_settings_relay.c',
            'sony_audio_volume_policy.c','sony_audio_jack.c','audio_hw_sony.c',
            'sony_audio_root_verify.c','sony_audio_route.c','sony_audio_controls.c',
            'sony_audio_cfw_profile.c','sony_audio_snapshot_store.c','sony_audio_snapshot_eq.c',
            'sony_audio_snapshot_tone.c'],
            ['libwalkdroid-sony-chain-client.so','libwalkdroid-sony-output-adapter.so',
             'libwalkdroid-sony-worker-process.so','libwalkdroid-sony-owner.so','libcrypto.so','libtinyalsa.so','libsony_ai_process.so'],
            defines=['SONY_SOUND_PRESETS','SONY_HAL_AI'])
        compile_one('lights.default.so',['lights_zx300a.c','sony_audio_cpu.c'],['libdl.so'])
        compile_one('sony-cpu-init',['sony_audio_cpu_init.c','sony_audio_cpu.c'],['libdl.so'],True)
        compile_one('sony-audio-dac-init',['sony_audio_dac_init.c'],['libtinyalsa.so'],True)
        compile_one('sony-audio-gain-tables',['sony_audio_gain_tables.c'],['libtinyalsa.so'],True)
        compile_one('sony-audio-gain-select',['sony_audio_gain_select.c','sony_audio_cfw_profile.c'],[],True)
        compile_one('libwalkdroid-sony-bt-catalog.so',[
            'sony_audio_bt_store.c','sony_audio_bt_peer_cache.c','sony_audio_bt_catalog.c'],
            ['libcrypto.so','libdl.so'])
        compile_one('sony-bt-catalogd',['sony_audio_bt_catalogd.c'],
            ['libwalkdroid-sony-bt-catalog.so','libcrypto.so','libdl.so'],True)
        compile_one('sony-bt-linkd',[
            'sony_audio_bt_linkd.c','sony_audio_bt_link_broker.c',
            'sony_audio_bt_ram_route.c','sony_audio_bt_broker_group.c','sony_audio_bt_tx_route_gate.c',
            'sony_audio_bt_tx_hal_config.c','sony_audio_bt_catalog.c','sony_audio_bt_store.c',
            'sony_audio_bt_peer_cache.c','sony_audio_bt_tx_link_client.c','sony_audio_bt_tx_link_process.c',
            'sony_audio_bt_tx_volume_client.c','sony_audio_bt_tx_data_server.c'],
            ['libcrypto.so','libdl.so'],True)
        compile_one('sony-bt-link-ctl',[
            'sony_audio_bt_link_ctl.c','sony_audio_bt_tx_data_client.c',
            'sony_audio_bt_tx_route_gate.c'],['libdl.so'],True)
        compile_one('audio.a2dp.default.so',[
            'audio_hw_sony_bt_link.c','sony_audio_bt_tx_data_client.c',
            'sony_audio_bt_tx_hal_config.c','sony_audio_bt_tx_route_gate.c'],['libcrypto.so','libdl.so'])
        compile_one('libwalkdroid-sony-usb-gadget-policy.so',['sony_audio_usb_gadget_policy.c'])
        compile_one('libwalkdroid-sony-usb-gadget-linux.so',['sony_audio_usb_gadget_linux.c'],
                    ['libwalkdroid-sony-owner.so','libtinyalsa.so'])
        compile_one('libwalkdroid-sony-usb-input-policy.so',['sony_audio_usb_input_policy.c'])
        compile_one('libwalkdroid-sony-usb-input-linux.so',['sony_audio_usb_input_linux.c','sony_audio_usb_boundary.c'],
                    ['libwalkdroid-sony-owner.so'])
        compile_one('libwalkdroid-sony-usb-events.so',['sony_audio_usb_stream.c','sony_audio_usb_monitor.c'])
        compile_one('libwalkdroid-sony-usb-capture.so',['sony_audio_usb_capture.c'],
                    ['libwalkdroid-sony-usb-events.so','libwalkdroid-sony-usb-input-linux.so','libtinyalsa.so'])
        compile_one('libwalkdroid-sony-usb-source-reader.so',['sony_audio_usb_source_reader.c'])
        compile_one('libwalkdroid-sony-receiver-pcm.so',['sony_audio_receiver_pcm.c'],['libwalkdroid-sony-usb-events.so'])
        compile_one('libwalkdroid-sony-receiver-client.so',['sony_audio_receiver_client.c'],['libwalkdroid-sony-receiver-pcm.so'])
        compile_one('libwalkdroid-sony-receiver-output.so',['sony_audio_receiver_output.c'],['libwalkdroid-sony-receiver-pcm.so'])
        compile_one('libwalkdroid-sony-receiver-protocol.so',['sony_audio_receiver_protocol.c'])
        compile_one('libwalkdroid-sony-pcm-sink-client.so',['sony_audio_pcm_sink_client.c'])
        compile_one('libwalkdroid-sony-receiver-playback.so',['sony_audio_receiver_playback.c'],
                    ['libwalkdroid-sony-receiver-client.so','libwalkdroid-sony-receiver-output.so','libwalkdroid-sony-pcm-sink-client.so'])
        compile_one('libwalkdroid-sony-receiver-process.so',['sony_audio_receiver_process.c'],
                    ['libwalkdroid-sony-receiver-client.so'])
        compile_one('libwalkdroid-sony-pcm-sink-process.so',['sony_audio_pcm_sink_process.c'],
                    ['libwalkdroid-sony-pcm-sink-client.so'])
        compile_one('libwalkdroid-sony-dsf-client.so',['sony_audio_dsf_client.c'])
        compile_one('libwalkdroid-sony-dsf-source.so',['sony_audio_dsf_source.c'])
        compile_one('libwalkdroid-sony-dsf-process.so',['sony_audio_dsf_process.c'],
                    ['libwalkdroid-sony-dsf-client.so'])
        compile_one('libwalkdroid-sony-dsf-playback.so',['sony_audio_dsf_playback.c'],
                    ['libwalkdroid-sony-dsf-client.so','libwalkdroid-sony-dsf-source.so',
                     'libwalkdroid-sony-pcm-sink-client.so'])
        compile_one('libwalkdroid-sony-dsf-route.so',[
            'sony_audio_dsf_route.c','sony_audio_owner_commands.c','sony_audio_route.c',
            'sony_audio_jack.c','sony_audio_controls.c','sony_audio_cfw_profile.c',
            'sony_audio_volume_policy.c','sony_audio_snapshot_store.c','sony_audio_snapshot_eq.c',
            'sony_audio_snapshot_tone.c','sony_audio_snapshot_dsd.c'],
            ['libwalkdroid-sony-owner.so','libwalkdroid-sony-chain-client.so','libtinyalsa.so','libcrypto.so'])
        compile_one('libwalkdroid-sony-dsf-owned.so',['sony_audio_dsf_owned.c'],
                    ['libwalkdroid-sony-dsf-route.so','libwalkdroid-sony-dsf-playback.so',
                     'libwalkdroid-sony-dsf-process.so','libwalkdroid-sony-dsf-client.so',
                     'libwalkdroid-sony-dsf-source.so','libwalkdroid-sony-pcm-sink-process.so',
                     'libwalkdroid-sony-pcm-sink-client.so','libwalkdroid-sony-owner.so',
                     'libwalkdroid-sony-chain-client.so','libtinyalsa.so','libcrypto.so'])
        compile_one('libwalkdroid-sony-dsd-power-platform.so',
                    ['sony_audio_dsd_power_linux.c','sony_audio_dsd_power_platform.c'],['libtinyalsa.so'])
        compile_one('libwalkdroid-sony-dsd-power-wire.so',['sony_audio_dsd_power_wire.c'])
        compile_one('libwalkdroid-sony-dsd-sink-client.so',['sony_audio_dsd_sink_client.c'])
        compile_one('libwalkdroid-sony-dsd-sink-process.so',['sony_audio_dsd_sink_process.c'],
                    ['libwalkdroid-sony-dsd-sink-client.so','libwalkdroid-sony-dsd-power-wire.so','libdl.so'])
        compile_one('libwalkdroid-sony-receiver-sink-process.so',['sony_audio_receiver_sink_process.c'],
                    ['libwalkdroid-sony-pcm-sink-client.so','libwalkdroid-sony-dsd-power-wire.so','libdl.so'])
        compile_one('libwalkdroid-sony-dsf-native-playback.so',['sony_audio_dsf_native_playback.c'],
                    ['libwalkdroid-sony-dsd-sink-client.so','libwalkdroid-sony-dsf-client.so',
                     'libwalkdroid-sony-dsf-source.so'])
        compile_one('libwalkdroid-sony-dsf-native-owned.so',['sony_audio_dsf_native_owned.c'],
                    ['libwalkdroid-sony-dsf-route.so','libwalkdroid-sony-dsf-native-playback.so',
                     'libwalkdroid-sony-dsf-process.so','libwalkdroid-sony-dsf-client.so',
                     'libwalkdroid-sony-dsf-source.so','libwalkdroid-sony-dsd-sink-process.so',
                     'libwalkdroid-sony-dsd-sink-client.so','libwalkdroid-sony-dsd-power-platform.so',
                     'libwalkdroid-sony-dsd-power-wire.so','libwalkdroid-sony-owner.so',
                     'libwalkdroid-sony-chain-client.so','libtinyalsa.so','libcrypto.so','libdl.so'])
        compile_one('sony-bt-receiverd',['sony_audio_bt_receiverd.c'],
                    ['libwalkdroid-sony-receiver-protocol.so','libwalkdroid-sony-owner.so',
                     'libwalkdroid-sony-dsf-route.so','libwalkdroid-sony-chain-client.so',
                     'libtinyalsa.so','libcrypto.so','libdl.so'],True)
        compile_one('libwalkdroid-sony-receiver-session.so',[
                    'sony_audio_receiver_session.c','sony_audio_receiver_idle_guard.c',
                    'sony_audio_receiver_idle_guard_linux.c','sony_audio_receiver_legacy_policy.c'],
                    ['libwalkdroid-sony-receiver-pcm.so','libwalkdroid-sony-receiver-client.so',
                     'libwalkdroid-sony-receiver-output.so','libwalkdroid-sony-receiver-playback.so',
                     'libwalkdroid-sony-receiver-process.so','libwalkdroid-sony-receiver-sink-process.so',
                     'libwalkdroid-sony-pcm-sink-client.so','libwalkdroid-sony-dsd-power-platform.so',
                     'libwalkdroid-sony-dsd-power-wire.so','libwalkdroid-sony-dsf-route.so',
                     'libwalkdroid-sony-owner.so','libwalkdroid-sony-chain-client.so',
                     'libtinyalsa.so','libcrypto.so','libdl.so'])
        compile_one('libwalkdroid-sony-usb-source-linux.so',['sony_audio_usb_source_linux.c'],
                    ['libwalkdroid-sony-usb-input-linux.so','libwalkdroid-sony-owner.so'])
        compile_one('libwalkdroid-sony-usb-ingress.so',['sony_audio_usb_ingress.c'],
                    ['libwalkdroid-sony-usb-input-linux.so','libwalkdroid-sony-usb-input-policy.so',
                     'libwalkdroid-sony-usb-capture.so','libwalkdroid-sony-usb-events.so',
                     'libwalkdroid-sony-owner.so','libtinyalsa.so'])
        compile_one('libwalkdroid-sony-usb-source-ingress.so',['sony_audio_usb_source_ingress.c'],
                    ['libwalkdroid-sony-usb-source-linux.so','libwalkdroid-sony-usb-source-reader.so',
                     'libwalkdroid-sony-usb-input-linux.so','libwalkdroid-sony-usb-input-policy.so',
                     'libwalkdroid-sony-usb-capture.so','libwalkdroid-sony-usb-events.so',
                     'libwalkdroid-sony-owner.so','libtinyalsa.so'])
        compile_one('libwalkdroid-sony-usb-duplex.so',['sony_audio_usb_duplex_sync.c'],
                    ['libwalkdroid-sony-usb-source-ingress.so','libwalkdroid-sony-usb-ingress.so',
                     'libwalkdroid-sony-usb-events.so','libwalkdroid-sony-receiver-session.so',
                     'libwalkdroid-sony-receiver-client.so','libwalkdroid-sony-owner.so',
                     'libwalkdroid-sony-dsf-route.so','libtinyalsa.so','libcrypto.so','libdl.so'],
                    defines=['WALKDROID_REQUIRE_USB_SOURCE=1','WALKDROID_USB_SETTINGS_OBSERVE=1',
                             'WALKDROID_USB_LIVE_VOLUME=1'])
        compile_one('libwalkdroid-sony-usb-mode.so',['sony_audio_usb_mode.c'],
                    ['libwalkdroid-sony-usb-duplex.so','libwalkdroid-sony-usb-gadget-linux.so',
                     'libwalkdroid-sony-usb-gadget-policy.so','libwalkdroid-sony-usb-events.so',
                     'libwalkdroid-sony-receiver-session.so','libwalkdroid-sony-dsd-power-platform.so',
                     'libwalkdroid-sony-cpu.so','libwalkdroid-sony-dsf-route.so',
                     'libwalkdroid-sony-owner.so','libtinyalsa.so','libcrypto.so','libdl.so'],
                    defines=['WALKDROID_REQUIRE_USB_SOURCE=1'])
        compile_one('sony-receiverd',['sony_audio_receiverd.c'],
                    ['libwalkdroid-sony-receiver-protocol.so','libwalkdroid-sony-usb-mode.so',
                     'libwalkdroid-sony-usb-duplex.so','libwalkdroid-sony-receiver-session.so',
                     'libwalkdroid-sony-dsf-route.so','libwalkdroid-sony-owner.so',
                     'libwalkdroid-sony-chain-client.so','libtinyalsa.so','libcrypto.so','libdl.so'],True)
        compile_one('libwalkdroid-sony-receiver-async.so',['sony_audio_receiver_async.c'],
                    ['libwalkdroid-sony-receiver-session.so','libwalkdroid-sony-receiver-pcm.so',
                     'libwalkdroid-sony-usb-events.so','libwalkdroid-sony-owner.so','libwalkdroid-sony-dsf-route.so',
                     'libtinyalsa.so','libcrypto.so','libdl.so'])
        compile_one('sony-bt-live-check',[
                    'sony_audio_bt_epoch_device_probe.c','sony_audio_bt_radio_owner.c',
                    'sony_audio_bt_epoch_receiver.c','sony_audio_bt_epoch_transport.c',
                    'sony_audio_bt_stream_epochs.c','sony_audio_bt_live_wire.c',
                    'sony_audio_bt_middleware_format.c'],
                    ['libwalkdroid-sony-receiver-async.so','libwalkdroid-sony-receiver-session.so',
                     'libwalkdroid-sony-receiver-client.so','libwalkdroid-sony-receiver-pcm.so',
                     'libwalkdroid-sony-dsd-power-platform.so','libwalkdroid-sony-cpu.so',
                     'libwalkdroid-sony-dsf-route.so','libwalkdroid-sony-owner.so',
                     'libwalkdroid-sony-chain-client.so','libtinyalsa.so','libcrypto.so','libdl.so'],True,
                    defines=['SONY_BT_CONTROLLER_PERIOD_WRITES=1','SONY_BT_CONTROLLER_LIVE_VOLUME=1'])
        compile_one('sony-dsf-playerd',['sony_audio_playerd.c','sony_audio_player_protocol.c',
                    'sony_audio_player_route_policy.c','sony_audio_dsf_reader.c','sony_audio_dsd_unpack.c'],
                    ['libwalkdroid-sony-dsf-owned.so','libwalkdroid-sony-dsf-native-owned.so',
                     'libwalkdroid-sony-dsf-source.so','libwalkdroid-sony-dsf-route.so',
                     'libwalkdroid-sony-dsf-client.so','libwalkdroid-sony-dsf-playback.so',
                     'libwalkdroid-sony-dsf-native-playback.so','libwalkdroid-sony-dsf-process.so',
                     'libwalkdroid-sony-pcm-sink-process.so','libwalkdroid-sony-pcm-sink-client.so',
                     'libwalkdroid-sony-dsd-sink-process.so','libwalkdroid-sony-dsd-sink-client.so',
                     'libwalkdroid-sony-dsd-power-platform.so','libwalkdroid-sony-dsd-power-wire.so',
                     'libwalkdroid-sony-owner.so','libwalkdroid-sony-chain-client.so',
                     'libtinyalsa.so','libcrypto.so','libdl.so'],True)
        compile_one('sony-bt-tx-supervisor',
                    ['sony_audio_bt_tx_link_supervisor.c','sony_audio_bt_radio_owner.c','sony_audio_route.c'],
                    ['libwalkdroid-sony-owner.so','libtinyalsa.so','libdl.so'],True)
        static_name='internal-ext4-dm-static'
        static_cmd=common+['-static','-fno-pie','-no-pie','-fno-builtin','-fno-unwind-tables',
            '-fno-asynchronous-unwind-tables','-Wl,-e,_start','-Wl,--build-id=none',
            str(src/'internal_ext4_dm_static.c'),'-lgcc','-o',str(work/static_name)]
        commands.append({'output':static_name,'sources':['internal_ext4_dm_static.c'],'libraries':[],'defines':[]})
        plans.append({'output':static_name,'libraries':[],'command':static_cmd})
        run_compiles(plans,env,args.jobs)
        for row in commands:
            shutil.copy2(work/row['output'],args.output/row['output'])
    version=tool_version([str(args.cc), '--version'], env=env)
    write_optional((args.output/'build.lock.json'), json.dumps({'compiler':str(args.cc),'version':version,
        'android_headers':str(args.android_headers),'android_libraries':str(args.android_libs),
        'android_crt':str(args.android_crt),'jobs':args.jobs,'components':commands},indent=2)+'\n',encoding='utf-8')
    print(f'Built {len(commands)} native components in {args.output}')


if __name__=='__main__':main()
