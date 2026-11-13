package org.walkdroid.sony.audio;

import java.io.IOException;
import java.math.BigDecimal;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/** Wire state only. A request or a cached switch value is never an ON/OFF receipt. */
final class SonyReceiverState {
    static final int MAGIC=0x52534457, OFF=0, STARTING=1, READY=2, STREAMING=3, FAULT=4, STOPPING=5, RETAINED=6;
    final int phase,error,rate,flags,cleanup;
    final long session,generation,sourceFrames,outputBytes;
    final boolean retained,enabled,known,busy;
    final byte[] wire;
    private SonyReceiverState(ByteBuffer r){
        wire=r.array().clone();
        phase=r.getInt(8);error=r.getInt(12);session=r.getLong(24);generation=r.getLong(32);
        sourceFrames=r.getLong(40);outputBytes=r.getLong(48);rate=r.getInt(56);flags=r.getInt(60);cleanup=r.getInt(64);
        retained=r.getInt(68)==1;enabled=(flags&1)!=0;
        known=phase==OFF||phase==READY||phase==STREAMING||phase==FAULT;
        busy=phase==STARTING||phase==STOPPING;
    }
    static SonyReceiverState decode(byte[] bytes,long request) throws IOException {
        if(bytes==null||bytes.length!=72||request<=0)throw new IOException("接收状态响应不完整");
        ByteBuffer r=ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
        if(r.getInt(0)!=MAGIC||r.getInt(4)!=1||r.getLong(16)!=request)throw new IOException("接收状态响应不匹配");
        SonyReceiverState s=new SonyReceiverState(r);int retained=r.getInt(68);
        if(s.phase<OFF||s.phase>RETAINED||s.error>0||s.cleanup>0||s.session<0||s.generation<0||s.sourceFrames<0||s.outputBytes<0||s.rate<0||s.rate>768000||(s.flags&~15)!=0||retained<0||retained>1)throw new IOException("接收状态响应无效");
        if(s.phase==OFF&&((s.flags&2)==0||s.enabled||s.retained||s.cleanup!=0))throw new IOException("关闭状态尚未确认");
        if(s.phase!=OFF&&s.session==0)throw new IOException("接收会话身份缺失");
        if((s.phase==READY||s.phase==STREAMING||s.phase==FAULT)&&(!s.enabled||s.generation==0||s.retained||(s.flags&2)!=0))throw new IOException("独占状态尚未确认");
        if(s.phase==FAULT&&s.error==0)throw new IOException("输入故障状态不完整");
        if(s.phase==RETAINED&&(!s.retained||(s.flags&2)!=0))throw new IOException("资源保留状态不完整");
        if(s.busy&&(s.flags&2)!=0)throw new IOException("退出过程尚未完成");
        return s;
    }
    boolean active(){return phase!=OFF;}
    String message(){
        switch(phase){
            case STARTING:return "正在开启 USB DAC…";
            case READY:return "已开启，等待电脑播放。";
            case STREAMING:return rate==0?"已开启，正在接收音频…":"正在播放 · "+BigDecimal.valueOf(rate,3).stripTrailingZeros().toPlainString()+" kHz";
            case FAULT:return error==-95?"电脑输入格式暂不支持；独占仍保留，请关闭后重新开启。":"音频输入中断；独占仍保留，请关闭后重新开启。";
            case STOPPING:return "正在关闭并恢复系统声音…";
            case RETAINED:return "退出状态未确认，请重启播放器。";
            default:return error==-6?"未开启，请先连接一副耳机。":error==-16?"未开启，请先停止其他播放。":error!=0?"未能开启，请检查耳机和电脑连接。":"已关闭；开启前请连接一副耳机并调整音量。";
        }
    }
}
