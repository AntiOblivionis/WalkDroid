#!/usr/bin/env python3
"""Apply WalkDroid's offline-player UI choices to the selected platform tree."""
from pathlib import Path


def write_if_changed(path: Path, text: str):
    if path.read_text(encoding='utf-8') != text:
        path.write_text(text, encoding='utf-8')


def integrate(platform: Path):
    settings = platform / 'packages/apps/Settings/src/com/android/settings/SettingsActivity.java'
    manifest = platform / 'packages/apps/Settings/AndroidManifest.xml'
    conditions = platform / 'packages/apps/Settings/src/com/android/settings/dashboard/conditional/ConditionManager.java'
    host = platform / 'frameworks/base/packages/SystemUI/src/com/android/systemui/qs/QSTileHost.java'
    footer = platform / 'frameworks/base/packages/SystemUI/src/com/android/systemui/qs/QSFooterImpl.java'
    settings_text = settings.read_text(encoding='utf-8')
    settings_text = settings_text.replace(
        'Settings.NetworkDashboardActivity.class.getName()),\n'
        '                !UserManager.isDeviceInDemoMode(this), isAdmin)',
        'Settings.NetworkDashboardActivity.class.getName()),\n'
        '                false, isAdmin)')
    write_if_changed(settings, settings_text)
    manifest_text = manifest.read_text(encoding='utf-8')
    manifest_text = manifest_text.replace(
        'android:name=".Settings$NetworkDashboardActivity"\n            android:label=',
        'android:name=".Settings$NetworkDashboardActivity"\n            android:enabled="false"\n            android:label=')
    write_if_changed(manifest, manifest_text)
    condition_text = conditions.read_text(encoding='utf-8')
    for name in ('AirplaneMode', 'Hotspot', 'CellularData', 'BackgroundData'):
        condition_text = condition_text.replace(f'        addIfMissing({name}Condition.class, conditions);\n', '')
    condition_text = condition_text.replace(
        'if (AirplaneModeCondition.class == clz) {\n'
        '            return new AirplaneModeCondition(this);\n'
        '        } else if (HotspotCondition.class == clz) {\n'
        '            return new HotspotCondition(this);\n'
        '        } else if (DndCondition.class == clz) {',
        'if (DndCondition.class == clz) {')
    for name in ('CellularData', 'BackgroundData'):
        condition_text = condition_text.replace(
            f'        }} else if ({name}Condition.class == clz) {{\n'
            f'            return new {name}Condition(this);\n', '')
    write_if_changed(conditions, condition_text)
    host_text = host.read_text(encoding='utf-8')
    marker = '        // WalkDroid offline player: do not show unsupported network tiles.\n'
    if marker not in host_text:
        host_text = host_text.replace(
            '        return tiles;\n    }\n}',
            marker +
            '        tiles.removeAll(Arrays.asList("wifi", "cell", "hotspot", "cast",\n'
            '                "nfc", "airplane", "usb_tether", "adb_network"));\n'
            '        return tiles;\n    }\n}')
    write_if_changed(host, host_text)
    footer_text = footer.read_text(encoding='utf-8')
    if 'mCarrierText.setVisibility(View.GONE);' not in footer_text:
        footer_text = footer_text.replace(
            '        mCarrierText = findViewById(R.id.qs_carrier_text);',
            '        mCarrierText = findViewById(R.id.qs_carrier_text);\n'
            '        mCarrierText.setVisibility(View.GONE);')
    write_if_changed(footer, footer_text)
    return {'settings': str(settings), 'manifest': str(manifest), 'conditions': str(conditions),
            'quick_settings': str(host), 'quick_settings_footer': str(footer)}
