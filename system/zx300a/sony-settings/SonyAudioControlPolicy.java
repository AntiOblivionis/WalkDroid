package org.walkdroid.sony.audio;

import java.util.LinkedHashMap;
import java.util.Map;

/** Correlates a historical receipt. Never substitutes it for current output state. */
public final class SonyAudioControlPolicy {
    private SonyAudioControlPolicy() {}
    public static Map<String, String> changes(Map<String, String> state, String key, String value) {
        if (!SonyAudioStatePolicy.editable(state, key)) throw new IllegalStateException("当前设置不可更改");
        Map<String, String> out = new LinkedHashMap<>();out.put(key, value);
        if (key.equals("sony_volume")) {
            int requested = Math.max(0, Math.min(120, Integer.parseInt(value))), limit = 120;
            try { limit = Math.max(0, Math.min(120, Integer.parseInt(state.get("sony_avls_limit")))); }
            catch (NumberFormatException ignored) { }
            String avls = state.get("sony_avls");
            out.put(key, Integer.toString("1".equals(avls) ? Math.min(requested, limit) : requested));
        }
        if (key.equals("sony_dc_mode")) out.put("sony_dc", "1");
        if (key.equals("sony_vinyl_mode")) out.put("sony_vinyl", "1");
        if (key.equals("sony_dsee_mode")) out.put("sony_dsee", "1");
        if ("1".equals(state.get("sony_readonly")) && key.equals("sony_selector") && value.equals("3") && "2".equals(state.get("sony_selector"))) {
            for (String field : new String[]{"sony_tone", "sony_low", "sony_mid", "sony_high"}) {
                if (state.containsKey(field)) out.put(field, state.get(field));
            }
        }
        return out;
    }
    public static String request(String generation, String revision, String id, Map<String, String> changes) {
        StringBuilder out = new StringBuilder("sony_owner_generation=").append(generation).append(";sony_revision=").append(revision).append(";sony_request_id=").append(id);
        for (Map.Entry<String, String> entry : changes.entrySet()) out.append(';').append(entry.getKey()).append('=').append(entry.getValue());
        return out.toString();
    }
    public static boolean confirmed(String raw, String id, String generation, String revision, Map<String, String> changes) {
        if (raw == null) return false;
        Map<String, String> fields = new LinkedHashMap<>();
        for (String pair : raw.split(";", -1)) {
            int equal = pair.indexOf('=');
            if (equal > 0) fields.put(pair.substring(0, equal), pair.substring(equal + 1));
        }
        String error=fields.get("sony_control_error");
        if(error!=null&&!error.isEmpty()&&!"0".equals(error))throw new IllegalStateException("控制操作失败（"+error+"）");
        return "3".equals(fields.get("sony_control_phase"));
    }
    public static String resultQuery(String id) {
        return "sony_control_result_" + id;
    }
}
