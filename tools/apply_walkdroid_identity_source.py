#!/usr/bin/env python3
"""Apply the current WalkDroid product's build identity to Android sources."""
from pathlib import Path


def integrate(platform: Path):
    makefile = platform/'build/make/core/Makefile'
    buildinfo = platform/'build/make/tools/buildinfo.sh'
    lineage = platform/'vendor/lineage/build/core/main_version.mk'
    text = makefile.read_text(encoding='utf-8')
    original = text
    marker = '# WalkDroid product build identity\n'
    if marker not in text:
        text = text.replace(
            '# A human-readable string that descibes this build in detail.\n',
            marker + 'ifeq ($(TARGET_PRODUCT),walkdroid_zx300a)\n'
            'WALKDROID_BUILD_LABEL ?= $(shell cat device/sony/zx300a/walkdroid-build)\n'
            'endif\n\n'
            '# A human-readable string that descibes this build in detail.\n')
        text = text.replace(
            '  BUILD_FINGERPRINT := $(PRODUCT_BRAND)/$(TARGET_PRODUCT)/$(TARGET_DEVICE):$(PLATFORM_VERSION)/$(BUILD_ID)/$(BF_BUILD_NUMBER):$(TARGET_BUILD_VARIANT)/$(BUILD_VERSION_TAGS)\n',
            '  ifeq ($(TARGET_PRODUCT),walkdroid_zx300a)\n'
            '    BUILD_FINGERPRINT := Sony/zx300a/zx300a:$(PLATFORM_VERSION)/$(BUILD_ID)/WalkDroid.$(WALKDROID_BUILD_LABEL).ZX300A:$(TARGET_BUILD_VARIANT)/$(BUILD_VERSION_TAGS)\n'
            '  else\n'
            '    BUILD_FINGERPRINT := $(PRODUCT_BRAND)/$(TARGET_PRODUCT)/$(TARGET_DEVICE):$(PLATFORM_VERSION)/$(BUILD_ID)/$(BF_BUILD_NUMBER):$(TARGET_BUILD_VARIANT)/$(BUILD_VERSION_TAGS)\n'
            '  endif\n')
        text = text.replace(
            '# Accepts a whitespace separated list of product locales such as\n',
            'ifeq ($(TARGET_PRODUCT),walkdroid_zx300a)\n'
            'BUILD_DISPLAY_ID := $(WALKDROID_BUILD_LABEL) ZX300A\n'
            'endif\n\n'
            '# Accepts a whitespace separated list of product locales such as\n')
    if text != original:
        makefile.write_text(text, encoding='utf-8')
    text = lineage.read_text(encoding='utf-8')
    original = text
    marker = '# WalkDroid product version\n'
    if marker not in text:
        text = text.replace(
            '# LineageOS System Version\n',
            marker + 'ifeq ($(TARGET_PRODUCT),walkdroid_zx300a)\n'
            'WALKDROID_BUILD_LABEL ?= $(shell cat device/sony/zx300a/walkdroid-build)\n'
            'LINEAGE_VERSION := 16.0-WalkDroid-$(WALKDROID_BUILD_LABEL)-ZX300A\n'
            'LINEAGE_DISPLAY_VERSION := $(LINEAGE_VERSION)\n'
            'endif\n\n# LineageOS System Version\n')
    if text != original:
        lineage.write_text(text, encoding='utf-8')
    text = buildinfo.read_text(encoding='utf-8')
    original = text
    text = text.replace(
        'echo "ro.build.user=$USER"\necho "ro.build.host=`hostname`"',
        'if [ "${TARGET_BUILD_FLAVOR%%-*}" = "walkdroid_zx300a" ]; then\n'
        '  echo "ro.build.user=${WALKDROID_BUILD_USER:-walkdroid}"\n'
        '  echo "ro.build.host=${WALKDROID_BUILD_HOST:-walkdroid}"\n'
        'else\n'
        '  echo "ro.build.user=$USER"\n'
        '  echo "ro.build.host=`hostname`"\n'
        'fi')
    text = text.replace('if [ "$PRODUCT_NAME" = "walkdroid_zx300a" ]; then',
                        'if [ "${TARGET_BUILD_FLAVOR%%-*}" = "walkdroid_zx300a" ]; then')
    if text != original:
        buildinfo.write_text(text, encoding='utf-8')
    return {'build_makefile': str(makefile), 'lineage_version': str(lineage),
            'buildinfo': str(buildinfo)}
