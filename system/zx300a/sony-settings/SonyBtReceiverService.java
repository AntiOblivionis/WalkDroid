package org.walkdroid.sony.audio;

/** Same receipt-driven lifetime and exclusive audio focus as USB, with an
 * explicitly separate native endpoint. No Android Bluetooth/audio decoder. */
public final class SonyBtReceiverService extends SonyUsbReceiverService {
    static final String ON="org.walkdroid.sony.audio.BT_RECEIVER_ON",OFF="org.walkdroid.sony.audio.BT_RECEIVER_OFF",REFRESH="org.walkdroid.sony.audio.BT_RECEIVER_REFRESH";
    @Override protected String receiverLabel(){return "蓝牙接收";}
    @Override protected String peerLabel(){return "手机";}
    @Override protected String channel(){return "sony-bt-receiver";}
    @Override protected int notificationId(){return 711;}
    @Override protected String controlSocket(){return "walkdroid.sony.bt.receiver.v1";}
    @Override protected int activateCommand(){return 4;}
    @Override protected int receiverHoldKind(){return 2;}
    @Override protected String onAction(){return ON;}
    @Override protected String offAction(){return OFF;}
    @Override protected String refreshAction(){return REFRESH;}
    @Override protected java.util.Map<String,String> readNativeVolume(SonyDsfVolumeControl.Port port,long generation)throws Exception{return SonyDsfVolumeControl.readBt(port,generation);}
    @Override protected int changeNativeVolume(SonyDsfVolumeControl.Port port,long generation,int delta,Integer absolute,String id)throws Exception{return SonyDsfVolumeControl.changeBt(port,generation,delta,absolute,id);}
    @Override protected String stateMessage(SonyReceiverState next){
        if(next.phase==SonyReceiverState.READY)return "已开启，请从已配对手机连接并播放。";
        if(next.phase==SonyReceiverState.STREAMING)return "蓝牙音频链路已开启 · "+java.math.BigDecimal.valueOf(next.rate,3).stripTrailingZeros().toPlainString()+" kHz";
        return super.stateMessage(next);
    }
}
