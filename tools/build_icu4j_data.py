#!/usr/bin/env python3
"""Generate Android ICU4J resource jars from the supplied ICU source tree."""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform-root', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True,
                        help='empty output directory for the ICU host build')
    parser.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
    args = parser.parse_args()
    platform = args.platform_root.resolve()
    source = platform / 'external/icu/icu4c/source'
    icu4j = platform / 'external/icu/icu4j'
    build = args.work_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(source / 'runConfigureICU'), 'Linux', '--disable-samples'], cwd=build, check=True)
    subprocess.run(['make', '-j' + str(args.jobs)], cwd=build, check=True)
    subprocess.run(['make', '-C', str(build / 'data'), 'ICU4J_ROOT=' + str(icu4j),
                    'icu4j-data-install'], check=True)
    subprocess.run(['make', '-C', str(build / 'test/testdata'), 'packagetest'], check=True)
    subprocess.run(['make', '-C', str(build / 'test/testdata'), 'ICU4J_ROOT=' + str(icu4j),
                    'icu4j-data-install'], check=True)
    print('Generated ICU4J data from', source)


if __name__ == '__main__':
    main()
