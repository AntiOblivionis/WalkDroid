#!/usr/bin/env python3
"""Build the selected LineageOS launcher from source without bundled Java jars."""
from __future__ import annotations
from pathlib import Path
import re
import shutil

ROOT = Path(__file__).resolve().parents[1]


def integrate(platform: Path):
    launcher = platform / 'packages/apps/Trebuchet'
    make = launcher / 'Android.mk'
    if not make.is_file():
        return {'status': 'Trebuchet source not present'}

    original = make.read_text(encoding='utf-8')
    text = original
    text = text.replace('LOCAL_STATIC_JAVA_LIBRARIES := libGoogleFeed\n',
                        'LOCAL_STATIC_JAVA_LIBRARIES :=\n')
    text = text.replace('    libSharedSystemUI \\\n    libGoogleFeed', '    SystemUISharedLib')
    text = text.replace('LOCAL_SDK_VERSION := system_current\n',
                        'LOCAL_PRIVATE_PLATFORM_APIS := true\n')
    if text != original:
        make.write_text(text, encoding='utf-8', newline='\n')

    source = ROOT / 'system/zx300a/android/launcher'
    target = launcher / 'src/com/android/launcher3/lineage'
    target.mkdir(parents=True, exist_ok=True)
    for name in ('LineageLauncherCallbacks.java', 'OverlayCallbackImpl.java'):
        new = (source / name).read_bytes()
        destination = target / name
        if not destination.exists() or destination.read_bytes() != new:
            shutil.copyfile(source / name, destination)

    settings = launcher / 'src/com/android/launcher3/SettingsActivity.java'
    if settings.is_file():
        before = settings.read_text(encoding='utf-8')
        value, count = re.subn(
            r'            if \(!LineageUtils\.hasPackageInstalled\(getActivity\(\),\s*'
            r'LineageLauncherCallbacks\.SEARCH_PACKAGE\)\) \{\s*'
            r'homeGroup\.removePreference\(minusOne\);\s*\}',
            '            homeGroup.removePreference(minusOne);', before, count=1)
        if value != before:
            settings.write_text(value, encoding='utf-8', newline='\n')
    else:
        count = 0
    return {'status': 'integrated', 'systemui_dependency': 'SystemUISharedLib',
            'google_feed_callback': 'excluded', 'google_feed_setting_removed': count}
