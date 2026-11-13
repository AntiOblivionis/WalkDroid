package org.walkdroid.audio.exclusive;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

/** Best-effort label for a persisted record; it is not an admission condition. */
final class OptionalBootLabel {
    static String read(File path) {
        byte[] raw=new byte[64];
        try(FileInputStream in=new FileInputStream(path)) {
            int size=in.read(raw);
            return size>0?new String(raw,0,size,StandardCharsets.US_ASCII).trim():"";
        } catch(IOException | SecurityException ignored) {
            return "";
        }
    }
    private OptionalBootLabel(){}
}
