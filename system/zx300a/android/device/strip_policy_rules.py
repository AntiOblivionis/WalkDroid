#!/usr/bin/env python3
"""Remove neverallow and ioctl-xperm statements from generated policy.conf.

The legacy kernel's policy database does not represent ioctl extended
permissions. Ordinary allow rules remain; ioctl access is correspondingly less
fine-grained. This operates on generated build output, not source policy files.
"""
from pathlib import Path
import re
import sys

RULE=re.compile(r'^\s*(?:neverallowxperm|dontauditxperm|auditallowxperm|allowxperm|neverallow)\b')


def transform(text):
    output=[];skipping=False
    for line in text.splitlines(keepends=True):
        if not skipping and RULE.match(line):skipping=True
        if not skipping:
            output.append(line);continue
        code,separator,comment=line.partition('#')
        end=code.find(';')
        if end>=0:
            skipping=False
            tail=code[end+1:]
            output.append(tail+(separator+comment if separator else ''))
        elif separator and not code.strip():
            output.append(separator+comment)
        elif line.endswith('\n'):
            output.append('\n')
    return ''.join(output)


def main():
    path=Path(sys.argv[1]);original=path.read_text(encoding='utf-8')
    path.write_text(transform(original),encoding='utf-8')


if __name__=='__main__':main()
