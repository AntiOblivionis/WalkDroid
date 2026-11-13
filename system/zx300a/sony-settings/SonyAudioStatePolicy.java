package org.walkdroid.sony.audio;

import java.util.LinkedHashMap;
import java.util.Map;

/** Parse available audio state and expose the fields present in that state. */
public final class SonyAudioStatePolicy {
    private SonyAudioStatePolicy() {}
    public static Map<String, String> parse(String raw) {
        Map<String, String> state = new LinkedHashMap<>();
        if (raw == null) return state;
        for (String pair : raw.split(";", -1)) {
            int index = pair.indexOf('=');
            if (index > 0) state.put(pair.substring(0, index), pair.substring(index + 1));
        }
        return state;
    }
    static boolean current(Map<String, String> state) {
        return state != null && "1".equals(state.get("sony_state_fresh"))
                && "0".equals(state.get("sony_state_error")) && "0".equals(state.get("sony_failed"));
    }
    static boolean editable(Map<String, String> state) {
        return state != null && !state.isEmpty();
    }
    public static boolean editable(Map<String, String> state, String key) {
        return editable(state) && state.containsKey(key);
    }
    static boolean presetEditable(Map<String,String> state){
        return editable(state);
    }
    static boolean nativeDsd(Map<String, String> state) { return state != null && "1".equals(state.get("sony_dsd_native")); }
    static boolean usbDac(Map<String, String> state) { return state != null && "usb_dac".equals(state.get("sony_source")); }
    static boolean btReceiver(Map<String, String> state) { return state != null && "bt_receiver".equals(state.get("sony_source")); }
    static boolean effectsVisible(Map<String, String> state) { return editable(state); }
    static boolean normalizerVisible(Map<String, String> state) { return effectsVisible(state) && state.containsKey("sony_normalizer"); }
    static String status(Map<String, String> state) {
        if (current(state) && btReceiver(state))return "蓝牙接收独占"+("1".equals(state.get("sony_output_ready"))?"输出已开启 · "+state.get("sony_output_rate")+" Hz"+("1".equals(state.get("sony_output_muted"))?" · 输出静音":"") : "已就绪 · 等待手机音频")+(editable(state,"sony_volume")?" · 原厂音量可调整 · 音效只读":" · 当前声音设置只读");
        if (current(state) && usbDac(state))
            return "USB DAC 独占" + ("1".equals(state.get("sony_output_ready"))
                    ? "输出 · " + state.get("sony_output_rate") + " Hz · " + ("1".equals(state.get("sony_output_muted")) ? "输出静音" : "正在播放")
                    : "已就绪 · 等待音频") + (editable(state,"sony_volume") ? " · 原厂音量可调整 · 音效只读" : " · 当前声音设置只读");
        if (current(state) && nativeDsd(state))
            return "原生 DSD 独占输出已就绪 · " + state.get("sony_output_rate") + " bit/s · "
                    + (editable(state) ? "原厂音量可调整" : "当前设置只读");
        if (current(state) && "1".equals(state.get("sony_readonly")))
            return "DSF 独占输出已就绪 · " + state.get("sony_output_rate") + " Hz · "
                    + (editable(state) ? "音效可调整 · 原厂音量可调整" : "当前设置只读");
        if (current(state)) return "已读取原厂声音设置 · " + ("1".equals(state.get("sony_active")) ? "正在播放" : "待机");
        if (state != null && "-16".equals(state.get("sony_state_error"))) return "声音设置正忙，稍后自动刷新。";
        return "尚未确认当前声音状态，正在重新读取。";
    }
}
