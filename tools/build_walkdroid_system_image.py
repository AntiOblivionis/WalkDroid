#!/usr/bin/env python3
"""Create a new local system image by overlaying a rebuilt framework on a base.

The input is a caller-provided raw ext4 Android system image. This command
copies the base into a new regular file, then writes the caller's services.jar
inside that copy with debugfs. It performs no device operations or mounts.
The Android platform itself is inherited from the base and is not rebuilt.
"""
import argparse
from walkdroid_build_metadata import write_optional, tool_version
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--base-system', type=Path, required=True, help='raw ext4 system image')
    ap.add_argument('--services-jar', type=Path, required=True, help='newly built framework jar')
    ap.add_argument('--output', type=Path, required=True, help='new local output image')
    ap.add_argument('--debugfs', default='debugfs')
    a = ap.parse_args()
    a.output.parent.mkdir(parents=True, exist_ok=True)
    with a.base_system.open('rb') as source, a.output.open('xb') as target:
        shutil.copyfileobj(source, target, 1024*1024)
    env = os.environ.copy(); env['LC_ALL'] = 'C'
    def debugfs(command):
        return subprocess.run([a.debugfs, '-w', '-R', command, str(a.output)], env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
    with tempfile.TemporaryDirectory(prefix='walkdroid-system-') as temporary:
        work = Path(temporary); jar = work/'services.jar'
        shutil.copyfile(a.services_jar, jar); jar.chmod(0o644)
        attributes = []
        listing = debugfs('ea_list /framework/services.jar').stdout
        for index,name in enumerate(re.findall(r'^\s+([^\s(]+)\s+\(\d+\)',listing,re.MULTILINE)):
            value = work/('attribute-'+str(index))
            debugfs('ea_get -f '+str(value)+' /framework/services.jar '+name)
            if value.is_file():attributes.append((name,value))
        # debugfs reports a nonexistent old file but still returns success;
        # the subsequent write can create it where the framework directory exists.
        debugfs('rm /framework/services.jar')
        result = debugfs('write '+str(jar)+' /framework/services.jar')
        # debugfs itself returns zero for several I/O failures. Its successful
        # write reports the allocated inode; this is an operation result, not
        # a hash, source identity or base-image compatibility requirement.
        if 'Allocated inode:' not in result.stdout:
            raise RuntimeError('debugfs could not write services.jar: '+result.stderr.strip())
        debugfs('set_inode_field /framework/services.jar uid 0')
        debugfs('set_inode_field /framework/services.jar gid 0')
        for name,value in attributes:
            debugfs('ea_set -f '+str(value)+' /framework/services.jar '+name)
    version = tool_version([a.debugfs, '-V'], env=env)
    lock = {'base_system':str(a.base_system), 'services_jar':str(a.services_jar), 'output':str(a.output),
            'debugfs':a.debugfs, 'tool_version':version}
    write_optional(a.output.with_name(a.output.name+'.build.lock.json'), json.dumps(lock, indent=2)+'\n', encoding='utf-8')
    print('Created local system image from supplied platform:', a.output)


if __name__ == '__main__':
    main()
