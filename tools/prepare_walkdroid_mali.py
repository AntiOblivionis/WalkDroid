#!/usr/bin/env python3
"""Prepare a caller-provided graphics library with explicitly chosen edits.

No known library version, digest, path, dependency or instruction is required.
Word patches are optional virtual-address/value pairs supplied by the builder;
they are applied without comparing the old bytes. Use only for an input whose
ABI and patch choices the builder understands. This tool never accesses a device.
"""
from __future__ import annotations

import argparse
from walkdroid_build_metadata import write_optional
import json
from pathlib import Path
import shutil
import struct
import subprocess


def word_offset(data: bytearray, address: int) -> tuple[int, str]:
    # Read only the ELF information needed to map a virtual address to bytes.
    cls, endian = data[4], data[5]
    order = '<' if endian == 1 else '>'
    if cls == 1:
        phoff = struct.unpack_from(order+'I',data,28)[0]
        size,count = struct.unpack_from(order+'HH',data,42)
        fmt = order+'8I'
        for n in range(count):
            kind,offset,vaddr,_,filesz,_,_,_ = struct.unpack_from(fmt,data,phoff+n*size)
            if kind == 1 and vaddr <= address and address+4 <= vaddr+filesz:
                return offset+address-vaddr,order
    elif cls == 2:
        phoff = struct.unpack_from(order+'Q',data,32)[0]
        size,count = struct.unpack_from(order+'HH',data,54)
        fmt = order+'II6Q'
        for n in range(count):
            kind,_,offset,vaddr,_,filesz,_,_ = struct.unpack_from(fmt,data,phoff+n*size)
            if kind == 1 and vaddr <= address and address+4 <= vaddr+filesz:
                return offset+address-vaddr,order
    raise ValueError('cannot map the supplied virtual address to file data')


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--input',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--patchelf',default='patchelf')
    ap.add_argument('--remove-needed',action='append',default=[])
    ap.add_argument('--patch-word',action='append',default=[],metavar='VIRTUAL_ADDRESS=VALUE')
    args=ap.parse_args()
    args.output.parent.mkdir(parents=True,exist_ok=True)
    if args.input.resolve()!=args.output.resolve():shutil.copyfile(args.input,args.output)
    if args.remove_needed:
        command=[args.patchelf]
        for name in args.remove_needed:command.extend(['--remove-needed',name])
        subprocess.run([*command,str(args.output)],check=True)
    if args.patch_word:
        data=bytearray(args.output.read_bytes())
        for item in args.patch_word:
            address,value=(int(part,0) for part in item.split('=',1))
            offset,order=word_offset(data,address)
            struct.pack_into(order+'I',data,offset,value)
        args.output.write_bytes(data)
    write_optional((args.output.parent/(args.output.name+'.build.lock.json')), json.dumps({
        'input':str(args.input),'output':str(args.output),'patchelf':args.patchelf,
        'removed_dependencies':args.remove_needed,'word_patches':args.patch_word,
    },indent=2)+'\n',encoding='utf-8')
    print('Prepared caller-supplied graphics library:',args.output)


if __name__=='__main__':main()
