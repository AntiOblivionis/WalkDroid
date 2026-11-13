#!/usr/bin/env python3
"""Build a caller-supplied Python 2 source tree into a private prefix."""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--cflags', help='C compiler flags, when needed by the selected host compiler')
    args = parser.parse_args()
    source, prefix, log = args.source.resolve(), args.prefix.resolve(), args.log.resolve()
    log.parent.mkdir(parents=True, exist_ok=True)
    print('Python toolchain build log:', log, flush=True)
    environment = os.environ.copy()
    if args.cflags:
        environment['CFLAGS'] = args.cflags
    with log.open('w', encoding='utf-8') as output:
        for command in ([str(source / 'configure'), '--prefix=' + str(prefix), '--without-ensurepip'],
                        ['make', '-j' + str(args.jobs)], ['make', 'install']):
            output.write('\n$ ' + ' '.join(command) + '\n')
            output.flush()
            result = subprocess.run(command, cwd=source, env=environment, stdout=output, stderr=subprocess.STDOUT, check=False)
            if result.returncode:
                raise SystemExit(result.returncode)
    print('Python toolchain installed:', prefix)


if __name__ == '__main__':
    main()
