#!/usr/bin/env python3
"""Build the Sony audio workers and libraries from the current sources.

Runs on Linux/WSL.  The Sony glibc libraries are supplied separately by the
builder; the generated WalkDroid libraries are always built in a fresh work
directory.  This command neither extracts nor distributes proprietary files.
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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sony-libs', type=Path, required=True,
                        help='directory containing the Sony glibc runtime libraries')
    parser.add_argument('--cc', type=Path, default=ROOT/'tools/cross-root/usr/bin/arm-linux-gnueabihf-gcc-15')
    parser.add_argument('--binutils-dir', type=Path, default=ROOT/'tools/cross-root/usr/bin')
    parser.add_argument('--host-lib-dir', type=Path, default=ROOT/'tools/cross-root/usr/lib/x86_64-linux-gnu')
    parser.add_argument('--gcc-include', type=Path, default=ROOT/'tools/cross-root/usr/lib/gcc-cross/arm-linux-gnueabihf/15/include')
    parser.add_argument('--glibc-include', type=Path, default=ROOT/'tools/sony-glibc223-headers/usr/arm-linux-gnueabihf/include')
    parser.add_argument('--jobs', type=int, default=default_jobs(12),
                        help='parallel Sony runtime outputs (default: available CPUs, up to 12)')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    args.sony_libs = args.sony_libs.resolve()
    if args.output is None:
        args.output = ROOT/'build/sony-runtime'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    args.output.mkdir(parents=True, exist_ok=True)
    source = ROOT/'system/zx300a'
    env = os.environ.copy()
    env['LD_LIBRARY_PATH'] = str(args.host_lib_dir) + ((':' + env['LD_LIBRARY_PATH']) if env.get('LD_LIBRARY_PATH') else '')
    components = []; plans = []
    with tempfile.TemporaryDirectory(prefix='walkdroid-sony-runtime-') as temp:
        work = Path(temp)
        binutils = work/'bin'
        binutils.mkdir()
        for name in ('as', 'ld'):
            (binutils/name).symlink_to((args.binutils_dir/('arm-linux-gnueabihf-' + name)).resolve())
        common = [str(args.cc), '-B'+str(binutils)+'/', '-B'+str(args.binutils_dir)+'/',
                  '-march=armv7-a', '-mfpu=neon', '-mfloat-abi=hard', '-std=gnu11',
                  '-O2', '-fno-strict-aliasing', '-fno-stack-protector', '-nostdlib', '-nostdinc',
                  '-isystem', str(args.glibc_include), '-isystem', str(args.gcc_include),
                  '-I'+str(source), '-Wall', '-Wextra', '-Wno-misleading-indentation']
        def compile_one(name, sources, libraries, *, executable=False, defines=(), exports=None):
            opts = (['-fno-pie', '-no-pie', '-Wl,--export-dynamic',
                     '-Wl,--dynamic-linker=/lib/ld-linux-armhf.so.3', '-Wl,-e,_start'] if executable
                    else ['-fPIC', '-shared', '-Wl,-soname,'+name, '-Wl,--no-undefined'])
            if exports is not None:
                export_file = work/(name+'.map')
                export_file.write_text(exports+'\n', encoding='utf-8')
                opts.append('-Wl,--version-script='+str(export_file))
            cmd = (common + ['-D'+d for d in defines] + opts + [str(source/s) for s in sources]
                   + ['-L'+str(work), '-L'+str(args.sony_libs), '-Wl,-rpath-link='+str(work),
                      '-Wl,-rpath-link='+str(args.sony_libs)] + ['-l:'+lib for lib in libraries]
                   + (['-l:ld-linux-armhf.so.3'] if not executable else [])
                   + ['-lgcc', '-o', str(work/name)])
            components.append({'output': name, 'sources': list(sources),
                               'libraries': list(libraries), 'defines': list(defines)})
            plans.append({'output': name, 'libraries': list(libraries), 'command': cmd})

        compile_one('libwalkdroid-sony-io-guard-v2.so', ['sony_audio_io_guard_v2.c'], ['libdl.so.2', 'libc.so.6'])
        compile_one('libwalkdroid-sony-chain.so', ['sony_audio_chain_bridge.c', 'sony_audio_native_call.S'],
                    ['libdl.so.2', 'libc.so.6'])
        compile_one('libsony_ai_client.so', ['sony_audio_ai_client.c', 'sony_audio_ai_wire.c'], ['libc.so.6'])
        compile_one('libsony_ai_bridge.so', ['sony_audio_ai_bridge.c', 'sony_audio_native_call.S'],
                    ['libc.so.6', 'libdl.so.2'],
                    exports='WALKDROID_SONY_AI_1.0 { global: sony_ai_bridge_open; sony_ai_bridge_set_enabled; '
                            'sony_ai_bridge_status; sony_ai_bridge_process; sony_ai_bridge_finish; '
                            'sony_ai_bridge_close; local: *; };')
        compile_one('libsony_ai_chain.so', ['sony_audio_ai_chain.c', 'sony_audio_pcm_boundary.c',
                    'sony_audio_chain_bridge.c', 'sony_audio_native_call.S'],
                    ['libsony_ai_client.so', 'libdl.so.2', 'ld-linux-armhf.so.3', 'libc.so.6'],
                    defines=['SONY_CHAIN_AI_BOUNDARY'],
                    exports='WALKDROID_SONY_AI_CHAIN_1.0 { global: sony_ai_chain_*; sony_chain_*; '
                            '_ZN3pst8services5sound6mobile3I2f18ProcessAudioPacketEPNS1_10DataPacketE; '
                            '_ZN3pst8services5sound6mobile12DseeHxCustom18ProcessAudioPacketEPNS1_10DataPacketE; local: *; };')
        worker_sources = ['sony_audio_feature_config.c', 'sony_audio_cfw_signature.c',
                          'sony_audio_cfw_worker_context.c', 'sony_audio_sound_presets.c',
                          'sony_audio_probe_start.S', 'sony_audio_stream_worker.c', 'sony_audio_stream.c',
                          'sony_audio_output_bridge.c', 'sony_audio_snapshot_config.c',
                          'sony_audio_snapshot_store.c', 'sony_audio_snapshot_eq.c',
                          'sony_audio_snapshot_tone.c', 'sony_audio_native_call.S']
        worker_defines = ['SONY_CFW_SIGNATURE_CONTEXT', 'SONY_SOUND_PRESETS']
        compile_one('sony-output-normal-worker', worker_sources,
                    ['libwalkdroid-sony-chain.so', 'libwalkdroid-sony-io-guard-v2.so', 'libdl.so.2', 'libc.so.6'],
                    executable=True, defines=worker_defines)
        compile_one('sony-output-ai-parent-worker', worker_sources+['sony_audio_ai_paused_power.c'],
                    ['libsony_ai_chain.so', 'libwalkdroid-sony-io-guard-v2.so', 'libdl.so.2',
                     'libsony_ai_client.so', 'libc.so.6'], executable=True,
                    defines=worker_defines+['SONY_AI_PARENT_RESOURCES', 'SONY_STREAM_AI'])
        compile_one('sony-output-ai-worker', worker_sources,
                    ['libsony_ai_chain.so', 'libwalkdroid-sony-io-guard-v2.so', 'libdl.so.2',
                     'libsony_ai_client.so', 'libc.so.6'], executable=True,
                    defines=worker_defines+['SONY_STREAM_AI'])
        compile_one('sony-ai-worker', ['sony_audio_ai_wire.c', 'sony_audio_ai_config.c',
                    'sony_audio_ai_worker.c', 'sony_audio_feature_config.c',
                    'sony_audio_native_call.S', 'sony_audio_probe_start.S'],
                    ['libsony_ai_bridge.so', 'libc.so.6', 'libdl.so.2'], executable=True)
        compile_one('sony-dsf-worker', ['sony_audio_dsf_worker.c', 'sony_audio_dsf_session.c',
                    'sony_audio_dsf_reader.c', 'sony_audio_dsd_unpack.c',
                    'sony_audio_dsd_render_bridge.c', 'sony_audio_dsd_fade_bridge.c',
                    'sony_audio_dsd_assembler.c', 'sony_audio_chain_bridge.c',
                    'sony_audio_native_call.S', 'sony_audio_native_call4.S',
                    'sony_audio_probe_start.S', 'sony_audio_snapshot_config.c',
                    'sony_audio_snapshot_store.c', 'sony_audio_snapshot_eq.c',
                    'sony_audio_snapshot_tone.c'], ['libc.so.6', 'libdl.so.2'],
                    executable=True, defines=['SONY_DSF_PRIVATE_CONFIG=1'])
        compile_one('sony-pcm-sink-worker', ['sony_audio_pcm_sink_worker.c',
                    'sony_audio_output_bridge.c', 'sony_audio_snapshot_config.c',
                    'sony_audio_snapshot_store.c', 'sony_audio_native_call.S',
                    'sony_audio_probe_start.S'],
                    ['libwalkdroid-sony-io-guard-v2.so', 'libc.so.6', 'libdl.so.2'], executable=True)
        compile_one('sony-receiver-worker', ['sony_audio_receiver_worker.c','sony_audio_probe_start.S',
                    'sony_audio_snapshot_store.c','sony_audio_snapshot_config.c',
                    'sony_audio_snapshot_eq.c','sony_audio_snapshot_tone.c'],
                    ['libwalkdroid-sony-chain.so','libc.so.6','libdl.so.2'], executable=True,
                    defines=['SONY_RECEIVER_PRIVATE_CONFIG=1'])
        compile_one('libwalkdroid-sony-dsd-power.so', ['sony_audio_dsd_power_bridge.c'],
                    ['libc.so.6', 'libdl.so.2'])
        compile_one('libsony-dsd-io-guard.so', ['sony_audio_dsd_io_guard.c'],
                    ['libc.so.6', 'libdl.so.2'])
        compile_one('libwalkdroid-sony-dsd-control-guard.so', ['sony_audio_dsd_control_guard.c'],
                    ['libc.so.6', 'libdl.so.2'])
        compile_one('libwalkdroid-sony-dsd-native-writer.so',
                    ['sony_audio_dsd_native_writer.c', 'sony_audio_native_call.S'],
                    ['libsony-dsd-io-guard.so', 'libc.so.6', 'libdl.so.2'])
        compile_one('libwalkdroid-sony-dsd-native-sink.so',
                    ['sony_audio_dsd_native_sink.c', 'sony_audio_native_call.S'],
                    ['libwalkdroid-sony-dsd-control-guard.so', 'libwalkdroid-sony-dsd-power.so',
                     'libwalkdroid-sony-dsd-native-writer.so', 'libsony-dsd-io-guard.so',
                     'libc.so.6', 'libdl.so.2'])
        compile_one('libwalkdroid-sony-dsd-power-wire.so', ['sony_audio_dsd_power_wire.c'],
                    ['libc.so.6', 'libdl.so.2'])
        compile_one('libsony-receiver-legacy-io.so', ['sony_audio_receiver_legacy_io.c'],
                    ['libc.so.6', 'libdl.so.2'])
        compile_one('libwalkdroid-sony-receiver-legacy-output.so',
                    ['sony_audio_receiver_legacy_output.c', 'sony_audio_native_call.S'],
                    ['libsony-receiver-legacy-io.so', 'libc.so.6', 'libdl.so.2'])
        compile_one('sony-receiver-legacy-worker',
                    ['sony_audio_receiver_legacy_worker.c', 'sony_audio_snapshot_config.c',
                     'sony_audio_snapshot_store.c', 'sony_audio_probe_start.S'],
                    ['libsony-receiver-legacy-io.so', 'libwalkdroid-sony-receiver-legacy-output.so',
                     'libwalkdroid-sony-dsd-power-wire.so', 'libc.so.6', 'libdl.so.2'], executable=True)
        compile_one('sony-dsd-sink-worker', ['sony_audio_dsd_sink_worker.c', 'sony_audio_probe_start.S'],
                    ['libwalkdroid-sony-dsd-native-sink.so', 'libwalkdroid-sony-dsd-power-wire.so',
                     'libc.so.6', 'libdl.so.2'], executable=True)
        compile_one('sony-bt-tx-worker', ['sony_audio_bt_tx_link.c', 'sony_audio_bt_tx_session.c',
                    'sony_audio_bt_components.c', 'sony_audio_bt_tx_pcm.c', 'sony_audio_srconv.c',
                    'sony_audio_bt_tx_packet_clock.c', 'sony_audio_bt_tx_volume.c',
                    'sony_audio_bt_tx_volume_client.c', 'sony_audio_bt_tx_volume_server.c',
                    'sony_audio_bt_tx_link_worker.c', 'sony_audio_probe_start.S'],
                    ['libpthread.so.0', 'librt.so.1', 'libdl.so.2', 'libc.so.6'], executable=True)
        compile_one('sony-bt-live-source', ['sony_audio_bt_epoch_adapter.c',
                    'sony_audio_bt_epoch_source.c', 'sony_audio_bt_epoch_transport.c',
                    'sony_audio_bt_stream_epochs.c', 'sony_audio_bt_live_wire.c',
                    'sony_audio_bt_middleware_format.c', 'sony_audio_bt_journal.c',
                    'sony_audio_bt_components.c', 'sony_audio_probe_start.S'],
                    ['libpthread.so.0', 'librt.so.1', 'libdl.so.2', 'libc.so.6'], executable=True)
        run_compiles(plans, env, args.jobs)
        for component in components:
            shutil.copy2(work/component['output'], args.output/component['output'])
    version = tool_version([str(args.cc), '--version'], env=env)
    write_optional((args.output/'build.lock.json'), json.dumps({'compiler': str(args.cc), 'version': version,
        'glibc_headers': str(args.glibc_include), 'sony_libraries': str(args.sony_libs), 'jobs': args.jobs,
        'components': components}, indent=2)+'\n', encoding='utf-8')
    print(f'Built {len(components)} Sony runtime components in {args.output}')


if __name__ == '__main__':
    main()
