package org.walkdroid.sony.audio;

import android.app.*;
import android.content.*;
import android.database.Cursor;
import android.media.*;
import android.media.session.*;
import android.net.*;
import android.os.*;
import android.provider.OpenableColumns;
import android.system.Os;
import java.io.*;
import java.nio.*;

/** File import and playback lifecycle. No AudioTrack, mixing, software volume,
 * DSP substitute, root shell, path-based privileged request or automatic Play. */
public final class SonyDsfPlayerService extends Service {
 public static final String SELECT="org.walkdroid.sony.SELECT_DSF",PLAY="org.walkdroid.sony.PLAY_DSF",STOP="org.walkdroid.sony.STOP_DSF",REFRESH="org.walkdroid.sony.REFRESH_DSF";
 private static final long MAX_FILE=8L*1024*1024*1024;
 private static final int MAGIC=0x50534457,STATUS=1,START=2,HALT=3;
 private HandlerThread thread;private Handler work;private volatile boolean destroyed;
 private volatile LocalSocket socket;private long request,session;private int phase;
 private File selected;private String title="尚未选择 DSF 文件";private volatile boolean importing,stopping;private boolean uncertain;
 private AudioManager audio;private PowerManager.WakeLock wake;private MediaSession media;
 private VolumeProvider nativeVolume;private long generation,lastVolumeRead;private volatile int confirmedVolume=-1;
 private String lastNativeEvent,mediaTitle,foregroundTitle,foregroundText;
 private int mediaPlaybackState=-1;private Boolean mediaActive;
 private boolean foreground,foregroundOngoing;
 private final SonyDsfVolumeControl.Port volumePort=new SonyDsfVolumeControl.Port(){
  public String get(String q)throws Exception{return (String)Class.forName("android.media.AudioSystem").getMethod("getParameters",String.class).invoke(null,q);}
  public int set(String q)throws Exception{return (Integer)Class.forName("android.media.AudioSystem").getMethod("setParameters",String.class).invoke(null,q);}
  public long now(){return SystemClock.elapsedRealtime();}public void pause()throws Exception{Thread.sleep(50);}
  public boolean alive(){return !destroyed&&phase==2;}
 };
 private final Binder binder=new LocalBinder();
 public static final class ViewState {
  public final String title,message,volume;public final boolean play,stop,select;
  ViewState(String t,String m,boolean p,boolean s,boolean c,String v){title=t;message=m;play=p;stop=s;select=c;volume=v;}
 }
 private volatile ViewState view=new ViewState("Sony Walkman ZX300A","正在读取播放状态…",false,false,false,"");
 public final class LocalBinder extends Binder {public SonyDsfPlayerService service(){return SonyDsfPlayerService.this;}}
 public ViewState state(){return view;}
 private boolean active(){return phase>=1&&phase<=3;}
 private void event(String text){try{
  File f=new File(getFilesDir(),"dsf-player-events.log");if(f.length()>65536){File old=new File(getFilesDir(),"dsf-player-events.previous.log");if(old.exists())old.delete();f.renameTo(old);}
  try(FileOutputStream out=new FileOutputStream(f,true)){out.write((SystemClock.elapsedRealtime()+" "+text+"\n").getBytes("UTF-8"));}
 }catch(Exception ignored){}}
 private Notification notification(String text){
  PendingIntent open=PendingIntent.getActivity(this,0,new Intent(this,SonyDsfPlayerActivity.class),PendingIntent.FLAG_UPDATE_CURRENT);
  PendingIntent stop=PendingIntent.getService(this,1,new Intent(this,SonyDsfPlayerService.class).setAction(STOP),PendingIntent.FLAG_UPDATE_CURRENT);
  Notification.Builder b=new Notification.Builder(this,"sony-dsf").setSmallIcon(android.R.drawable.ic_media_play).setContentTitle(title).setContentText(WalkDroidText.translate(this,text)).setContentIntent(open).setOnlyAlertOnce(true).setOngoing(active()||importing);
  if(active()||importing)b.addAction(android.R.drawable.ic_media_pause,WalkDroidText.translate(this,"停止"),stop);
  return b.build();
 }
 private synchronized void foreground(String text,boolean force){
  if(destroyed)return;boolean ongoing=active()||importing;
  if(force||!foreground||ongoing!=foregroundOngoing||!title.equals(foregroundTitle)||!text.equals(foregroundText)){
   startForeground(37,notification(text));foreground=true;foregroundTitle=title;foregroundText=text;foregroundOngoing=ongoing;
  }
 }
 private synchronized void clearForeground(){if(foreground){stopForeground(true);foreground=false;foregroundTitle=foregroundText=null;}}
 private synchronized void publishMedia(){
  if(destroyed||media==null)return;
  int state=phase==2?PlaybackState.STATE_PLAYING:active()?PlaybackState.STATE_BUFFERING:PlaybackState.STATE_STOPPED;
  if(state!=mediaPlaybackState){media.setPlaybackState(new PlaybackState.Builder().setActions(PlaybackState.ACTION_PLAY|PlaybackState.ACTION_STOP).setState(state,PlaybackState.PLAYBACK_POSITION_UNKNOWN,1).build());mediaPlaybackState=state;}
  if(!title.equals(mediaTitle)){media.setMetadata(new MediaMetadata.Builder().putString(MediaMetadata.METADATA_KEY_TITLE,title).build());mediaTitle=title;}
  boolean active=active();if(mediaActive==null||mediaActive!=active){media.setActive(active);mediaActive=active;}
 }
 private void publish(String text){
  boolean busy=active()||importing,canPlay=selected.isFile()&&!busy&&!uncertain,canSelect=!busy&&!uncertain;
  String volume=phase==2?(confirmedVolume>=0?"原厂音量 "+confirmedVolume+" / 120":"原厂音量待确认"):"";
  ViewState old=view;if(!title.equals(old.title)||!text.equals(old.message)||!volume.equals(old.volume)||canPlay!=old.play||busy!=old.stop||canSelect!=old.select){
   view=new ViewState(title,text,canPlay,busy,canSelect,volume);event("STATE phase="+phase+" session="+session+" uncertain="+uncertain+" "+text);
  }
  if(busy)foreground(text,false);else clearForeground();publishMedia();
 }
 @Override public void onCreate(){super.onCreate();
  ((NotificationManager)getSystemService(NOTIFICATION_SERVICE)).createNotificationChannel(new NotificationChannel("sony-dsf",WalkDroidText.translate(this,"Sony DSD 播放"),NotificationManager.IMPORTANCE_LOW));
  selected=new File(getFilesDir(),"sony-selected.dsf");title=getSharedPreferences("sony-player",0).getString("title",title);
  audio=(AudioManager)getSystemService(AUDIO_SERVICE);wake=((PowerManager)getSystemService(POWER_SERVICE)).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK,"WalkDroid:SonyDSF");wake.setReferenceCounted(false);
  media=new MediaSession(this,"SonyDSF");media.setCallback(new MediaSession.Callback(){@Override public void onPlay(){work.post(()->play());}@Override public void onStop(){work.post(()->stop());}});
  thread=new HandlerThread("SonyDSFControl");thread.start();work=new Handler(thread.getLooper());
  nativeVolume=new VolumeProvider(VolumeProvider.VOLUME_CONTROL_ABSOLUTE,120,0){
   @Override public void onAdjustVolume(int direction){if(direction==1||direction==-1)work.post(()->changeVolume(direction,null));}
   @Override public void onSetVolumeTo(int value){work.post(()->changeVolume(0,value));}
  };
  media.setPlaybackToRemote(nativeVolume);work.post(()->refresh());
 }
 @Override public IBinder onBind(Intent i){return binder;}
 @Override public int onStartCommand(Intent i,int flags,int id){
  if(i!=null){String action=i.getAction();Uri uri=i.getData();
   if(STOP.equals(action)&&importing)stopping=true;
   // A foreground-service start still needs its immediate notification even if
   // the same cached view was previously published. Track this actual start so
   // a refused/no-op action can also remove the temporary notification.
   if(PLAY.equals(action)||SELECT.equals(action))foreground(SELECT.equals(action)?"正在准备文件…":"正在启动…",true);
   work.post(()->{if(destroyed)return;try{if(SELECT.equals(action))select(uri);else if(PLAY.equals(action))play();else if(STOP.equals(action))stop();else if(REFRESH.equals(action))refresh();}
    finally{if(!active()&&!importing)clearForeground();}});
  }return START_NOT_STICKY;
 }
 private void connect() throws Exception {
  if(socket!=null)return;LocalSocket next=new LocalSocket(LocalSocket.SOCKET_SEQPACKET);
  try{next.connect(new LocalSocketAddress("walkdroid.sony.player.v1",LocalSocketAddress.Namespace.ABSTRACT));next.setSoTimeout(3000);
   synchronized(this){if(destroyed)throw new IOException("播放服务已退出");socket=next;request=0;}
  }catch(Exception e){next.close();throw e;}
 }
 private ByteBuffer exchange(int command,long wanted,FileDescriptor source) throws Exception {
  connect();if(destroyed)throw new IOException("播放服务已退出");long id=++request;
  ByteBuffer packet=ByteBuffer.allocate(32).order(ByteOrder.LITTLE_ENDIAN);packet.putInt(MAGIC).putInt(1).putInt(command).putInt(0).putLong(id).putLong(wanted);
  LocalSocket current=socket;current.setFileDescriptorsForSend(source==null?null:new FileDescriptor[]{source});
  try{current.getOutputStream().write(packet.array());}finally{current.setFileDescriptorsForSend(null);}
  byte[] raw=new byte[80];new DataInputStream(current.getInputStream()).readFully(raw);
  return ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN);
 }
 private void accepted(ByteBuffer r) throws Exception {if(r.getInt(12)!=0)throw new IOException("播放后端返回错误 "+r.getInt(12));}
 private void observe(ByteBuffer r){
  String nativeEvent="NATIVE phase="+r.getInt(8)+" error="+r.getInt(12)+" session="+r.getLong(24)+" rate="+r.getInt(48)+" eof="+r.getInt(52)+" cleanup="+r.getInt(56)+" retained="+r.getInt(60)+" generation="+r.getLong(64)+" revision="+r.getInt(72)+" flags="+r.getInt(76);
  if(!nativeEvent.equals(lastNativeEvent)){event(nativeEvent);lastNativeEvent=nativeEvent;}
  phase=r.getInt(8);session=r.getLong(24);generation=r.getLong(64);boolean failed=phase==6;uncertain=failed&&((r.getInt(76)&8)!=0||r.getInt(60)!=0||r.getInt(56)!=0);
  if(phase==2&&SystemClock.elapsedRealtime()-lastVolumeRead>=1000)refreshVolume();
  if(active()){publish(phase==2?"正在播放 · "+SonyDsfPlayerFormat.description(r.getInt(48),r.getInt(76)):phase==3?"正在停止…":"正在启动原厂播放链…");work.postDelayed(poll,250);}
  else{stopping=false;finishAudio();publish(failed?"未能播放（错误 "+r.getInt(12)+"）":phase==4?"播放结束":phase==5?"已停止":selected.isFile()?"文件已准备好":"请选择 DSF 文件");}
 }
 private final Runnable poll=()->{if(!destroyed&&active())try{observe(exchange(STATUS,0,null));}catch(Exception e){failed(e);}};
 private void refreshVolume(){lastVolumeRead=SystemClock.elapsedRealtime();try{
  confirmedVolume=Integer.parseInt(SonyDsfVolumeControl.read(volumePort,generation).get("sony_volume"));if(nativeVolume.getCurrentVolume()!=confirmedVolume)nativeVolume.setCurrentVolume(confirmedVolume);
 }catch(Exception e){confirmedVolume=-1;}}
 private void changeVolume(int direction,Integer absolute){
  if(destroyed||phase!=2)return;
  try{String id=java.util.UUID.randomUUID().toString().replace("-","");event("VOLUME_REQUEST generation="+generation+" direction="+direction+" absolute="+absolute);
   int value=SonyDsfVolumeControl.change(volumePort,generation,direction,absolute,id);confirmedVolume=value;if(nativeVolume.getCurrentVolume()!=value)nativeVolume.setCurrentVolume(value);
   event("VOLUME_APPLIED generation="+generation+" value="+value+" request="+id);
  }catch(Exception e){confirmedVolume=-1;event("VOLUME_UNCONFIRMED "+e);}
 }
 private final AudioManager.OnAudioFocusChangeListener focus=change->{if(change==AudioManager.AUDIOFOCUS_LOSS||change==AudioManager.AUDIOFOCUS_LOSS_TRANSIENT||change==AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK)work.post(()->stop());};
 private synchronized void finishAudio(){if(wake!=null&&wake.isHeld())wake.release();if(audio!=null)audio.abandonAudioFocus(focus);}
 private void failed(Exception e){
  work.removeCallbacks(poll);uncertain=true;phase=6;event("ERROR "+e);try{if(socket!=null)socket.close();}catch(Exception ignored){}socket=null;
  finishAudio();publish("播放状态未确认，请刷新状态后再试");
 }
 private void refresh(){if(importing)return;try{ByteBuffer r=exchange(STATUS,0,null);observe(r);}catch(Exception e){failed(e);}}
 private void play(){
  if(destroyed||active()||importing||uncertain||!selected.isFile())return;
  try{ByteBuffer prior=exchange(STATUS,0,null);
   int oldPhase=prior.getInt(8);boolean unsafe=oldPhase==6&&((prior.getInt(76)&8)!=0||prior.getInt(60)!=0||prior.getInt(56)!=0);
   if((oldPhase>=1&&oldPhase<=3)||unsafe)throw new IOException("播放链尚未释放");
   if(audio.requestAudioFocus(focus,AudioManager.STREAM_MUSIC,AudioManager.AUDIOFOCUS_GAIN)!=AudioManager.AUDIOFOCUS_REQUEST_GRANTED)throw new IOException("无法取得播放焦点");
   synchronized(this){if(destroyed)throw new IOException("播放服务已退出");wake.acquire();}
   try(ParcelFileDescriptor fd=ParcelFileDescriptor.open(selected,ParcelFileDescriptor.MODE_READ_ONLY)){
    ByteBuffer r=exchange(START,0,fd.getFileDescriptor());accepted(r);observe(r);
   }
  }catch(Exception e){failed(e);}
 }
 private void stop(){
  if(importing){stopping=true;return;}
  if(!active()||stopping)return;stopping=true;
  try{ByteBuffer r=exchange(HALT,session,null);accepted(r);observe(r);}catch(Exception e){failed(e);}
 }
 private void select(Uri uri){
  if(active()||importing||uncertain){publish("请先停止播放并确认状态");return;}
  if(uri==null||!"content".equals(uri.getScheme())){publish("请选择可读取的 DSF 文件");return;}
  File temporary=new File(getFilesDir(),"sony-selected-import.tmp");importing=true;stopping=false;publish("正在准备 DSF 文件…");
  try{
   String name="DSF 文件";try(Cursor c=getContentResolver().query(uri,new String[]{OpenableColumns.DISPLAY_NAME},null,null,null)){if(c!=null&&c.moveToFirst())name=c.getString(0);}
   if(name==null||name.length()>255||!name.toLowerCase(java.util.Locale.ROOT).endsWith(".dsf"))throw new IOException("请选择 .dsf 文件");
   if(temporary.exists()&&!temporary.delete())throw new IOException("无法清理未完成文件");
   long limit=Math.min(MAX_FILE,new StatFs(getFilesDir().getPath()).getAvailableBytes()-64L*1024*1024),count=0;if(limit<92)throw new IOException("存储空间不足");
   try(InputStream in=getContentResolver().openInputStream(uri);FileOutputStream out=new FileOutputStream(temporary)){
    if(in==null)throw new IOException("无法读取文件");byte[] buffer=new byte[65536];int n;
    while((n=in.read(buffer))!=-1){if(destroyed||stopping)throw new InterruptedIOException("已取消准备");count+=n;if(count>limit)throw new IOException("文件超过可用空间");out.write(buffer,0,n);}out.getFD().sync();
   }
   if(count<92)throw new IOException("DSF 文件不完整");Os.chmod(temporary.getPath(),0600);
   if(!temporary.renameTo(selected))throw new IOException("无法保存已准备文件");
   title=name;getSharedPreferences("sony-player",0).edit().putString("title",name).apply();event("IMPORTED bytes="+count);importing=false;stopping=false;publish("文件已准备好");
  }catch(Exception e){temporary.delete();importing=false;stopping=false;event("IMPORT_ERROR "+e);publish(e instanceof InterruptedIOException?"已取消准备":"无法准备文件："+e.getMessage());}
 }
 @Override public void onDestroy(){destroyed=true;work.removeCallbacksAndMessages(null);try{if(socket!=null)socket.close();}catch(Exception ignored){}socket=null;finishAudio();clearForeground();synchronized(this){if(media!=null)media.release();media=null;}thread.quitSafely();super.onDestroy();}
}
