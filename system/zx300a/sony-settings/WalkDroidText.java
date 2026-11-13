package org.walkdroid.sony.audio;

import android.content.Context;
import android.content.res.Resources;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** Translates both local UI text and status text received from platform services. */
final class WalkDroidText {
    private WalkDroidText() {}

    private static final class Entry {
        final String source, translated;
        Entry(String source, String translated) { this.source = source; this.translated = translated; }
    }
    private static String cachedLocale;
    private static Map<Character, List<Entry>> entries;

    private static synchronized Map<Character, List<Entry>> entries(Context context) {
        Resources r = context.getResources();
        String locale = r.getConfiguration().getLocales().toLanguageTags();
        if (entries != null && locale.equals(cachedLocale)) return entries;
        int sourceId = r.getIdentifier("walkdroid_message_source", "array", context.getPackageName());
        int textId = r.getIdentifier("walkdroid_messages", "array", context.getPackageName());
        String[] source = r.getStringArray(sourceId), translated = r.getStringArray(textId);
        Map<Character, List<Entry>> result = new HashMap<>();
        for (int i = 0; i < source.length && i < translated.length; i++) {
            if (source[i].isEmpty()) continue;
            Character first = source[i].charAt(0);
            List<Entry> bucket = result.get(first);
            if (bucket == null) { bucket = new ArrayList<>(); result.put(first, bucket); }
            bucket.add(new Entry(source[i], translated[i]));
        }
        for (List<Entry> bucket : result.values())
            Collections.sort(bucket, (a, b) -> Integer.compare(b.source.length(), a.source.length()));
        cachedLocale = locale; entries = result; return result;
    }

    static String translate(Context context, String original) {
        if (original == null || original.isEmpty()) return original;
        Map<Character, List<Entry>> dictionary = entries(context);
        StringBuilder result = new StringBuilder(original.length());
        for (int i = 0; i < original.length();) {
            List<Entry> candidates = dictionary.get(original.charAt(i));
            Entry matched = null;
            if (candidates != null) for (Entry candidate : candidates) {
                if (original.startsWith(candidate.source, i)) { matched = candidate; break; }
            }
            if (matched == null) { result.append(original.charAt(i++)); continue; }
            result.append(matched.translated); i += matched.source.length();
            // Preserve a device/app name supplied after these service status prefixes.
            if (matched.source.equals("正在连接 ") || matched.source.equals("独占已生效：")
                    || matched.source.equals("独占已恢复：") || matched.source.equals("保留应用选择：")) {
                result.append(original.substring(i)); break;
            }
        }
        return result.toString();
    }
}
