# Removable exFAT support

The Android platform checkout supplies `external/exfat` (fuse-exfat, GPL-2.0
or later) and `external/ntfs-3g`'s `libfuse-lite` (LGPL-2.0). Their original
copyright notices and license files remain in those source trees. Staging copies
the selected platform's exFAT frontend and substitutes this directory's
libfuse-lite entry point; it also removes an upstream compile-time FUSE version
gate. No precompiled exFAT component is required. A distributor must provide
the corresponding third-party source and comply with those licenses.

The present experimental mount is read-only so the card's existing contents
are not altered during device acceptance.
