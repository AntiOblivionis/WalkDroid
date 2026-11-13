#!/usr/bin/env python3
"""Integrate the optional legacy-kernel policy transformation into a checkout."""
from __future__ import annotations
import argparse
from datetime import datetime,timezone
from pathlib import Path
import re
import shutil

ROOT=Path(__file__).resolve().parents[1]
MARKER='# WalkDroid generated-policy transformation'


def integrate(platform: Path, backup_dir: Path | None = None):
    path=platform/'system/sepolicy/definitions.mk'
    if not path.is_file():return {'status':'definitions file missing'}
    text=path.read_text(encoding='utf-8')
    if MARKER in text:return {'status':'already present'}
    match=re.search(r'(?ms)^define transform-policy-to-conf\s*\n.*?^endef\s*$',text)
    if not match:return {'status':'policy transform not found'}
    backup=backup_dir or ROOT/'backups'/('walkdroid-platform-policy-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ'))
    backup.mkdir(parents=True,exist_ok=True);shutil.copy2(path,backup/'definitions.mk')
    addition='''# WalkDroid generated-policy transformation
$(hide) $(if $(WALKDROID_SEPOLICY_FILTER),python3 $(WALKDROID_SEPOLICY_FILTER) $@,true)
'''
    body=match.group(0);position=body.rfind('endef')
    replacement=body[:position]+addition+body[position:]
    path.write_text(text[:match.start()]+replacement+text[match.end():],encoding='utf-8',newline='\n')
    return {'status':'inserted','backup':str(backup/'definitions.mk')}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform-root',type=Path,required=True)
    parser.add_argument('--backup-dir',type=Path)
    args=parser.parse_args();print(integrate(args.platform_root.resolve(),args.backup_dir))


if __name__=='__main__':main()
