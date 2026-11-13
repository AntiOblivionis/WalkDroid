package org.walkdroid.sony.audio;

import java.util.Map;

/** The saved request, a live AI worker and actual PCM geometry are distinct. */
final class SonyDseeState {
    private static final String[] KEYS={"sony_ai_available","sony_ai_processing","sony_pcm_input_rate","sony_pcm_output_rate"};
    private SonyDseeState() {}
    static boolean available(Map<String,String> state) {return state!=null&&"1".equals(state.get("sony_ai_available"));}
    static void validate(Map<String,String> state) {
        boolean present=false;for(String key:KEYS)present|=state.containsKey(key);if(!present)return;
        for(String key:KEYS)if(!state.containsKey(key))throw new IllegalStateException("DSEE 处理状态不完整");
        for(String key:new String[]{KEYS[0],KEYS[1]})if(!state.get(key).matches("0|1"))throw new IllegalStateException("DSEE 状态标记无效");
        for(String key:new String[]{KEYS[2],KEYS[3]})if(!state.get(key).matches("0|8000|11025|12000|16000|22050|24000|32000|44100|48000|88200|96000|176400|192000|352800|384000"))throw new IllegalStateException("DSEE 采样率无效");
        if("1".equals(state.get("sony_ai_processing"))){
            String input=state.get(KEYS[2]),output=state.get(KEYS[3]);
            if(!available(state)||!"1".equals(state.get("sony_dsee"))||!"1".equals(state.get("sony_dsee_mode"))
                    ||!"0".equals(state.get("sony_direct"))||!"1".equals(state.get("sony_active"))
                    ||!"1".equals(state.get("sony_owner_self"))||!"0".equals(state.get("sony_readonly"))
                    ||!input.matches("44100|48000")||!("0".equals(output)||output.equals("44100".equals(input)?"176400":"192000")))
                throw new IllegalStateException("DSEE AI 实际处理状态不一致");
        }
    }
    static String summary(Map<String,String> state) {
        if(!"1".equals(state.get("sony_dsee")))return "已关闭";
        if(available(state)&&"1".equals(state.get("sony_dsee_mode"))){
            if("1".equals(state.get("sony_ai_processing")))return "DSEE AI 正在处理 · 输入 "+state.get("sony_pcm_input_rate")+" Hz"
                    +("0".equals(state.get("sony_pcm_output_rate"))?" · 等待输出":" · 输出 "+state.get("sony_pcm_output_rate")+" Hz");
            return "1".equals(state.get("sony_active"))?"当前格式按 Sony 原厂 DSEE HX 规则处理"+geometry(state):"已选择 DSEE AI · 适用于 44.1 / 48 kHz 立体声 PCM";
        }
        return "1".equals(state.get("sony_active"))?"DSEE HX · 按 Sony 原厂适用条件处理"+geometry(state):"已选择 DSEE HX · 播放时按原厂适用条件处理";
    }
    private static String geometry(Map<String,String> state) {
        String input=state.get("sony_pcm_input_rate"),output=state.get("sony_pcm_output_rate");
        if(input==null||"0".equals(input))return "";
        return " · 输入 "+input+" Hz"+(output==null||"0".equals(output)?" · 等待输出":" · 输出 "+output+" Hz");
    }
}
