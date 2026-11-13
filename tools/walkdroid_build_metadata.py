"""Best-effort metadata for builds; never determines whether a build succeeds."""
from pathlib import Path
import subprocess
import sys


def tool_version(command, env=None):
    try:
        result = subprocess.run(command, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                encoding='utf-8', errors='replace', check=False, timeout=5)
    except (OSError, subprocess.TimeoutExpired):
        return None
    if result.returncode:
        return None
    lines = (result.stdout or result.stderr).splitlines()
    return lines[0] if lines else None


def write_optional(path, text, encoding='utf-8'):
    try:
        Path(path).write_text(text, encoding=encoding)
        return True
    except OSError as exc:
        print(f'Could not write the optional build record {path}: {exc}', file=sys.stderr, flush=True)
        return False
