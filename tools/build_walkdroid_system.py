#!/usr/bin/env python3
"""Build the selected Android system image from the current source tree.

This entry uses the same caller-selected platform checkout and build environment
as build_walkdroid_platform.py, with systemimage as its default target. It does
not select a device or reuse a historical boot image.
"""
from build_walkdroid_platform import main


if __name__ == '__main__':
    main(default_targets=['systemimage'], default_product='walkdroid_zx300a-userdebug')
