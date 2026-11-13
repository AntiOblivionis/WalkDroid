#!/usr/bin/env python3
"""Provide caller-supplied Android build tools in a writable platform tree.

The source release intentionally omits checked-in tool binaries. This prepares
only the build environment; it does not copy any Sony firmware into a release.
"""
import argparse
import os
from pathlib import Path
import shutil

TOOL_FILES = (
    'external/annotation-tools/annotation-file-utilities/annotation-file-utilities.jar',
    'external/dagger2/lib/auto-common-1.0-20151022.071545-39.jar',
    'external/dagger2/lib/auto-factory-1.0-20150915.183854-35.jar',
    'external/dagger2/lib/auto-service-1.0-rc2.jar',
    'external/dagger2/lib/auto-value-1.4.1.jar',
    'external/dagger2/lib/google-java-format-0.1-20151017.042846-2.jar',
    'external/dagger2/lib/javax-inject.jar',
    'external/dexmaker/lib/dalvik-dx-1.jar',
    'external/dexmaker/lib/libcore-dex-2.jar',
    'external/error_prone/error_prone/error_prone_annotations-2.2.0.jar',
    'external/guice/lib/build/munge.jar',
    'external/jarjar/lib/apache-ant-1.9.4.jar',
    'external/jarjar/lib/maven-plugin-api.jar',
    'external/libmojo/jni/java/lang/Runtime.class',
    'external/mockito/lib/byte-buddy-1.7.9.jar',
    'external/mockito/lib/byte-buddy-agent-1.7.9.jar',
    'external/proguard/lib/proguard.jar',
    'development/host/windows/prebuilt/usb/AdbWinApi.dll',
    'development/host/windows/prebuilt/usb/AdbWinUsbApi.dll',
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tools-source', type=Path, required=True,
                        help='caller-provided Android build-tool checkout')
    parser.add_argument('--platform-root', type=Path, required=True,
                        help='writable extracted Android source tree')
    parser.add_argument('--copy', action='store_true', help='copy tools instead of linking them')
    parser.add_argument('--extra', action='append', default=[],
                        help='additional tool file relative to the supplied checkout; repeat as needed')
    args = parser.parse_args()
    source = args.tools_source.resolve()
    target = args.platform_root.resolve()

    def place(src, dst):
        src, dst = Path(src), Path(dst)
        dst.parent.mkdir(parents=True, exist_ok=True)
        if dst.exists() or dst.is_symlink():
            return str(dst)
        # Some upstream toolchains contain optional dangling compatibility
        # links. Preserve those links instead of trying to open their targets.
        if src.is_symlink() and not src.exists():
            os.symlink(os.readlink(src), dst)
            return str(dst)
        with src.open('rb') as stream:
            is_script = stream.read(2) == b'#!'
        # Some tool scripts resolve their own path to find adjacent resources.
        # Copy scripts so that the Android build's installation layout works.
        if args.copy or is_script or src.name in ('Android.bp', 'Android.mk'):
            shutil.copy2(src, dst)
        else:
            os.symlink(str(src.resolve()), dst)
        return str(dst)

    shutil.copytree(source / 'prebuilts', target / 'prebuilts', dirs_exist_ok=True, symlinks=True,
                    ignore=shutil.ignore_patterns('.git'), copy_function=place)
    for name in (*TOOL_FILES, *args.extra):
        place(source / name, target / name)
    for src in (source / 'external/kotlinc/lib').glob('*.jar'):
        place(src, target / 'external/kotlinc/lib' / src.name)
    print('Prepared Android build tools in', target)


if __name__ == '__main__':
    main()
