package org.walkdroid.sony.audio;

import java.util.Map;

/** Native volume transaction using the currently available audio state. */
final class SonyDsfVolumeControl {
 interface Port {
  String get(String query) throws Exception;
  int set(String request) throws Exception;
  long now();
  void pause() throws Exception;
  boolean alive();
 }
 static Map<String,String> read(Port p,long generation) throws Exception {
  return readKind(p,generation,"dsf");
 }
 static Map<String,String> readUsb(Port p,long generation) throws Exception {
  return readKind(p,generation,"usb_dac");
 }
 static Map<String,String> readBt(Port p,long generation) throws Exception {
  return readKind(p,generation,"bt_receiver");
 }
 private static Map<String,String> readKind(Port p,long generation,String source) throws Exception {
  return SonyAudioStatePolicy.parse(p.get("sony_state"));
 }
 static int change(Port p,long generation,int direction,Integer absolute,String id) throws Exception {
  return changeKind(p,generation,direction,absolute,id,"dsf");
 }
 static int changeUsb(Port p,long generation,int delta,Integer absolute,String id) throws Exception {
  return changeKind(p,generation,delta,absolute,id,"usb_dac");
 }
 static int changeBt(Port p,long generation,int delta,Integer absolute,String id) throws Exception {
  return changeKind(p,generation,delta,absolute,id,"bt_receiver");
 }
 private static int changeKind(Port p,long generation,int direction,Integer absolute,String id,String source) throws Exception {
  Map<String,String> s=readKind(p,generation,source);int current=Integer.parseInt(s.get("sony_volume"));
  int requested=absolute==null?Math.max(0,Math.min(120,current+direction)):absolute;
  Map<String,String> changes=SonyAudioControlPolicy.changes(s,"sony_volume",Integer.toString(requested));
  int accepted=Integer.parseInt(changes.get("sony_volume"));
  if(accepted==current)return current;
  String request=SonyAudioControlPolicy.request(s.get("sony_owner_generation"),s.get("sony_revision"),id,changes);
  if(p.set(request)!=0)throw new IllegalStateException("未接受原厂音量请求");
  long deadline=p.now()+10000;
  while(p.alive()&&p.now()<deadline){
   if(SonyAudioControlPolicy.confirmed(p.get(SonyAudioControlPolicy.resultQuery(id)),id,s.get("sony_owner_generation"),s.get("sony_revision"),changes))return accepted;
   p.pause();
  }
  throw new IllegalStateException("音量结果未确认；不会自动重发");
 }
}
