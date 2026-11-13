#!/usr/bin/env python3
"""Adapt GraphicBuffer layout for an unmodified legacy Mali userspace ABI.

Android 9 adds transport counts ahead of the buffer identifier. The older
Mali userspace ABI reads that identifier at the Android 6 position. Keeping
the platform's own identifier before the new counters restores that position
without editing a proprietary driver. All platform consumers must be rebuilt.
"""
from pathlib import Path


def integrate(platform: Path):
    header = platform / 'frameworks/native/libs/ui/include/ui/GraphicBuffer.h'
    if not header.is_file():
        return {'source': str(header), 'changed': False}
    source = header.read_text(encoding='utf-8')
    old = ('    // numbers of fds/ints in native_handle_t to flatten\n'
           '    uint32_t mTransportNumFds;\n'
           '    uint32_t mTransportNumInts;\n\n'
           '    uint64_t mId;\n')
    new = ('    // Keep the Android 6 identifier position used by legacy Mali.\n'
           '    uint64_t mId;\n\n'
           '    // numbers of fds/ints in native_handle_t to flatten\n'
           '    uint32_t mTransportNumFds;\n'
           '    uint32_t mTransportNumInts;\n')
    result = source.replace(old, new)
    if result != source:
        header.write_text(result, encoding='utf-8')
    return {'source': str(header), 'changed': result != source}
