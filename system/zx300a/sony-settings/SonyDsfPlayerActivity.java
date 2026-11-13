package org.walkdroid.sony.audio;
import android.app.Activity;
import android.content.*;
import android.net.Uri;
import android.os.*;
import android.widget.*;

/** File entry for the actual Sony native/conversion chain. */
public final class SonyDsfPlayerActivity extends Activity {
 private String tr(String text){return WalkDroidText.translate(this,text);}
 private SonyDsfPlayerService service;private TextView file,status,volume;private Button choose,play,stop;private boolean bound,resumed;private final Handler main=new Handler();
 private final ServiceConnection connection=new ServiceConnection(){public void onServiceConnected(ComponentName n,IBinder b){service=((SonyDsfPlayerService.LocalBinder)b).service();render();}public void onServiceDisconnected(ComponentName n){service=null;}};
 private final Runnable update=new Runnable(){public void run(){if(resumed){render();main.postDelayed(this,300);}}};
 private Button button(LinearLayout box,int id,String title,Runnable action){Button b=new Button(this);b.setId(id);b.setText(title);b.setSoundEffectsEnabled(false);b.setOnClickListener(v->action.run());box.addView(b,new LinearLayout.LayoutParams(-1,-2));return b;}
 private void send(String action){startService(new Intent(this,SonyDsfPlayerService.class).setAction(action));}
 @Override public void onCreate(Bundle state){super.onCreate(state);setTitle(tr("Sony DSD 播放"));
  LinearLayout box=new LinearLayout(this);box.setOrientation(1);int pad=(int)(12*getResources().getDisplayMetrics().density);box.setPadding(pad,pad,pad,pad);
  file=new TextView(this);file.setTextSize(18);box.addView(file);status=new TextView(this);status.setTextSize(14);box.addView(status);
  volume=new TextView(this);volume.setId(0x572006);box.addView(volume);
  choose=button(box,0x572001,tr("选择 DSF 文件"),()->{Intent i=new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");startActivityForResult(i,31);});
  play=button(box,0x572002,tr("播放"),()->send(SonyDsfPlayerService.PLAY));stop=button(box,0x572003,tr("停止"),()->send(SonyDsfPlayerService.STOP));
  button(box,0x572004,tr("声音设置"),()->startActivity(new Intent(this,SonyAudioSettings.class)));button(box,0x572005,tr("刷新播放状态"),()->send(SonyDsfPlayerService.REFRESH));
  TextView note=new TextView(this);note.setText(tr("3.5 mm 使用 DSD 转 PCM，滤波器和 DSD 增益可在声音设置中调整。4.4 mm 与原生 DSD 尚未完成。"));box.addView(note);
  ScrollView scroll=new ScrollView(this);scroll.addView(box);setContentView(scroll);bound=bindService(new Intent(this,SonyDsfPlayerService.class),connection,BIND_AUTO_CREATE);viewIntent(getIntent());
 }
 private void viewIntent(Intent i){if(Intent.ACTION_VIEW.equals(i.getAction())&&i.getData()!=null)select(i.getData());}
 private void select(Uri uri){startForegroundService(new Intent(this,SonyDsfPlayerService.class).setAction(SonyDsfPlayerService.SELECT).setData(uri).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION));}
 @Override protected void onActivityResult(int request,int result,Intent data){super.onActivityResult(request,result,data);if(request==31&&result==RESULT_OK&&data!=null&&data.getData()!=null)select(data.getData());}
 @Override protected void onNewIntent(Intent i){super.onNewIntent(i);setIntent(i);viewIntent(i);}
 private static void text(TextView view,String value){if(!value.contentEquals(view.getText()))view.setText(value);}
 private void render(){SonyDsfPlayerService.ViewState s=service==null?null:service.state();text(file,s==null?"Sony Walkman ZX300A":("尚未选择 DSF 文件".equals(s.title)?tr(s.title):s.title));text(status,s==null?tr("正在读取播放状态…"):tr(s.message));text(volume,s==null?"":tr(s.volume));choose.setEnabled(s!=null&&s.select);play.setEnabled(s!=null&&s.play);stop.setEnabled(s!=null&&s.stop);}
 @Override protected void onResume(){super.onResume();resumed=true;main.post(update);}
 @Override protected void onPause(){resumed=false;main.removeCallbacks(update);super.onPause();}
 @Override protected void onDestroy(){if(bound)unbindService(connection);super.onDestroy();}
}
