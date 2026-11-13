"""Adapt platform Slice consumers to the source-built support library."""
from pathlib import Path
import re


def update(path: Path, transform):
    original = path.read_text(encoding='utf-8')
    changed = transform(original)
    if changed != original:
        path.write_text(changed, encoding='utf-8')


def integrate(platform: Path):
    settings = platform / 'packages/apps/Settings/src/com/android/settings'
    for name in (
        'bluetooth/BluetoothSliceBuilder.java', 'location/LocationSliceBuilder.java',
        'notification/ZenModeSliceBuilder.java', 'wifi/WifiSliceBuilder.java',
        'wifi/calling/WifiCallingSliceHelper.java', 'slices/SliceBuilderUtils.java',
    ):
        def adapt_builders(text):
            for method, builder in (('addRow', 'RowBuilder'), ('addInputRange', 'InputRangeBuilder')):
                text = re.sub(r'\.' + method + r'\(\s*(\w+)\s*->\s*\1\b',
                              '.' + method + '(new ListBuilder.' + builder + '()', text)
            return (text.replace('.setColor(R.color.material_blue_500)',
                                 '.setAccentColor(R.color.material_blue_500)')
                    .replace('.setKeywords(keywords)', '.setKeywords(new java.util.LinkedHashSet<>(keywords))')
                    .replace('.setTitleItem(icon)', '.setTitleItem(icon, ListBuilder.ICON_IMAGE)'))
        update(settings / name, adapt_builders)

    def adapt_search(text):
        return (text.replace('SliceItem headerItem = content.getHeaderItem();',
                             'SliceItem headerItem = content.getHeader() != null\n'
                             '                ? content.getHeader().getSliceItem() : null;')
                .replace('headerItem = content.getRowItems().get(0);',
                         'headerItem = content.getRowItems().get(0).getSliceItem();'))
    update(settings / 'search/DeviceIndexUpdateJobService.java', adapt_search)

    systemui = platform / 'frameworks/base/packages/SystemUI/src/com/android'
    def adapt_keyguard_view(text):
        return (text.replace('mHasHeader = lc.hasHeader();',
                             'mHasHeader = lc.getHeader() != null\n'
                             '                && !lc.getHeader().getSliceItem().hasHint("list_item");')
                .replace('SliceItem subItem = lc.getRowItems().get(i);',
                         'SliceItem subItem = lc.getRowItems().get(i).getSliceItem();')
                .replace('subItems.get(0),\n                    true /* showStartItem */)',
                         'subItems.get(0),\n                    0 /* header position */)')
                .replace('item, true /* showStartItem */)', 'item, i)'))
    update(systemui / 'keyguard/KeyguardSliceView.java', adapt_keyguard_view)

    def adapt_keyguard_provider(text):
        text = text.replace('import androidx.core.graphics.drawable.IconCompat;',
                            'import android.support.v4.graphics.drawable.IconCompat;')
        if 'import android.support.v4.graphics.drawable.IconCompat;' not in text:
            text = text.replace('import androidx.slice.Slice;',
                                'import android.support.v4.graphics.drawable.IconCompat;\nimport androidx.slice.Slice;')
        text = text.replace('new ListBuilder(getContext(), mSliceUri)',
                            'new ListBuilder(getContext(), mSliceUri, ListBuilder.INFINITY)')
        text = text.replace('builder.addRow(new RowBuilder(builder, mDateUri).setTitle(mLastText));',
                            'builder.setHeader(new ListBuilder.HeaderBuilder(mDateUri).setTitle(mLastText));')
        for uri in ('Uri.parse(KEYGUARD_ACTION_URI)', 'mAlarmUri', 'mDndUri'):
            text = text.replace('new RowBuilder(builder, ' + uri + ')', 'new RowBuilder(' + uri + ')')
        text = text.replace('.addEndItem(alarmIcon);',
                            '.addEndItem(IconCompat.createFromIcon(alarmIcon), ListBuilder.ICON_IMAGE);')
        text = text.replace('.addEndItem(Icon.createWithResource(getContext(), R.drawable.stat_sys_dnd));',
                            '.addEndItem(IconCompat.createWithResource(getContext(), R.drawable.stat_sys_dnd),\n'
                            '                        ListBuilder.ICON_IMAGE);')
        return text
    update(systemui / 'systemui/keyguard/KeyguardSliceProvider.java', adapt_keyguard_provider)
