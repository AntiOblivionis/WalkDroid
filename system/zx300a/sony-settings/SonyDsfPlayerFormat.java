package org.walkdroid.sony.audio;

/** Units are selected only by the broker's native-format receipt. Admission
 * counters include Sony preroll/tail; they are not a music seek position. */
final class SonyDsfPlayerFormat {
    private SonyDsfPlayerFormat() {}
    static boolean nativeDsd(int flags) { return (flags & 16) != 0; }
    static String description(int rate, int flags) {
        return nativeDsd(flags) ? "原生 DSD · DSD" + (rate / 44100)
                : "原厂 DSD 转 PCM · " + rate + " Hz";
    }
    static long admittedMillis(long bytes, int rate, int flags) {
        long perSecond = nativeDsd(flags) ? rate / 4 : rate * 8L;
        return perSecond == 0 ? 0 : (bytes / perSecond) * 1000L + bytes % perSecond * 1000L / perSecond;
    }
}
