#!/usr/bin/env python3
"""Integrate the removable exFAT frontend into the selected Android source."""
from pathlib import Path
import re


def integrate(platform: Path):
    base=platform/'system/vold'
    public=base/'model/PublicVolume.cpp'
    exfat=base/'fs/Exfat.cpp'
    result={}
    if public.is_file():
        text=public.read_text(encoding='utf-8')
        original=text
        text,n=re.subn(r'    if \(!IsFilesystemSupported\(mFsType\)\) \{\n'
                       r'.*?\n    \}\n', '', text, count=1, flags=re.S)
        text,k=re.subn(r'    if \(mFsType == "exfat"\) \{\n'
                       r'        ret = exfat::Check\(mDevPath\);\n'
                       r'    \} else if \(mFsType == "ext4"\) \{',
                       '    if (mFsType == "ext4") {',text,count=1)
        if text!=original:public.write_text(text,encoding='utf-8',newline='\n')
        result['public_volume']={'filesystem_gate_removed':n,'exfat_fsck_removed':k}
    else:
        result['public_volume']='source file missing'
    if exfat.is_file():
        text=exfat.read_text(encoding='utf-8')
        original=text
        text,n=re.subn(r'bool IsSupported\(\) \{.*?\n\}',
                       'bool IsSupported() {\n    return true;\n}',text,count=1,flags=re.S)
        replacement='''status_t Mount(const std::string& source, const std::string& target, int ownerUid, int ownerGid,
               int permMask) {
    // Experimental card contents remain untouched; the FUSE process opens the
    // backing device read-only. The user may supply a different mount policy.
    std::vector<std::string> cmd = {
        "/system/bin/mount.exfat-fuse", "-o",
        StringPrintf("ro,uid=%d,gid=%d,umask=%o", ownerUid, ownerGid, permMask),
        source, target,
    };
    return ForkExecvp(cmd) == 0 ? 0 : -1;
}'''
        text,k=re.subn(r'status_t Mount\(const std::string& source, const std::string& target, '
                       r'int ownerUid, int ownerGid,\n\s*int permMask\) \{.*?\n\}',
                       lambda _:replacement,text,count=1,flags=re.S)
        if text!=original:exfat.write_text(text,encoding='utf-8',newline='\n')
        result['exfat']={'support_gate_removed':n,'fuse_mount_selected':k}
    else:
        result['exfat']='source file missing'
    return result
