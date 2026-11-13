#!/usr/bin/env python3
"""Create a new local ext4 image from a caller-provided data tree or tarball.

Runs on Linux/WSL. This does not connect to a player, open a block device, mount
the image, or alter the supplied tree. Android ownership/labels and kernel
compatibility depend on the supplied staging metadata and platform inputs.
"""
import argparse
from walkdroid_build_metadata import write_optional, tool_version
import json
from pathlib import Path
import shutil
import stat
import subprocess
import tarfile
import tempfile


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--data-tree', type=Path, required=True, help='staging directory')
    ap.add_argument('--size-bytes', type=int, required=True)
    ap.add_argument('--output', type=Path, required=True, help='new output image file')
    ap.add_argument('--work-dir', type=Path,
                    help='optional directory for temporary image creation before copying to output')
    ap.add_argument('--mke2fs', default='mke2fs')
    ap.add_argument('--metadata-tar', type=Path,
                    help='root-level tar whose UID, GID and modes are applied to the image')
    ap.add_argument('--debugfs', default='debugfs', help='ext filesystem metadata editor')
    ap.add_argument('--features', default='^64bit,^metadata_csum,^orphan_file',
                    help='mke2fs feature selection; override for the target kernel')
    ap.add_argument('--mkfs-option', action='append', default=[], help='additional mke2fs argument; repeat as needed')
    a = ap.parse_args()
    a.output.parent.mkdir(parents=True, exist_ok=True)
    temporary_image = (tempfile.TemporaryDirectory(prefix='walkdroid-data-image-', dir=a.work_dir)
                       if a.work_dir is not None else None)
    image_path = Path(temporary_image.name)/'data.img' if temporary_image else a.output
    # Exclusive creation keeps an existing file or device node untouched.
    with image_path.open('xb') as image:
        image.truncate(a.size_bytes)
    command = [a.mke2fs, '-F', '-t', 'ext4', '-b', '4096', '-O', a.features,
               '-d', str(a.data_tree), *a.mkfs_option, str(image_path)]
    subprocess.run(command, check=True)
    metadata_output = None
    if a.metadata_tar:
        kinds = {tarfile.REGTYPE:stat.S_IFREG, tarfile.AREGTYPE:stat.S_IFREG,
                 tarfile.DIRTYPE:stat.S_IFDIR, tarfile.SYMTYPE:stat.S_IFLNK,
                 tarfile.LNKTYPE:stat.S_IFREG, tarfile.CHRTYPE:stat.S_IFCHR,
                 tarfile.BLKTYPE:stat.S_IFBLK, tarfile.FIFOTYPE:stat.S_IFIFO}
        with tarfile.open(a.metadata_tar) as archive, tempfile.TemporaryDirectory(prefix='walkdroid-ext4-metadata-') as temporary:
            commands = Path(temporary)/'debugfs.commands'
            with commands.open('w', encoding='utf-8') as stream:
                for member in archive:
                    name = member.name.removeprefix('./')
                    path = '/' if name in ('', '.') else '/'+name
                    path = path.replace('\\', '\\\\').replace('"', '\\"')
                    mode = kinds.get(member.type, stat.S_IFREG) | (member.mode & 0o7777)
                    for key, value in (('uid', str(member.uid)), ('gid', str(member.gid)),
                                       ('mode', '0'+format(mode, 'o'))):
                        stream.write(f'set_inode_field "{path}" {key} {value}\n')
            result = subprocess.run([a.debugfs, '-w', '-f', str(commands), str(image_path)],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    text=True, check=True)
            metadata_output = result.stdout
    if temporary_image:
        with image_path.open('rb') as source, a.output.open('xb') as destination:
            shutil.copyfileobj(source, destination, 4*1024*1024)
        temporary_image.cleanup()
    if metadata_output is not None:
        write_optional(a.output.with_name(a.output.name+'.metadata.log'), metadata_output, encoding='utf-8')
    version = tool_version([a.mke2fs, '-V'])
    lock = {'data_tree':str(a.data_tree), 'size_bytes':a.size_bytes, 'output':str(a.output),
            'mke2fs':a.mke2fs, 'tool_version':version,
            'features':a.features, 'mkfs_options':a.mkfs_option,
            'metadata_tar':str(a.metadata_tar) if a.metadata_tar else None,
            'metadata_editor':a.debugfs if a.metadata_tar else None,
            'work_directory':str(a.work_dir) if a.work_dir is not None else None}
    write_optional(a.output.with_name(a.output.name+'.build.lock.json'), json.dumps(lock, indent=2)+'\n', encoding='utf-8')
    print('Created local data image:', a.output)


if __name__ == '__main__':
    main()
