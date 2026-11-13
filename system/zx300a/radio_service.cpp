// Direct real transports: MediaTek STP H4 and Sony CXD22xx NCI 1.0.
#include <android/hardware/nfc/1.0/INfc.h>
#include <android/hardware/bluetooth/1.0/IBluetoothHci.h>
#include <hidl/HidlTransportSupport.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <log/log.h>
#include <stdio.h>
#include <cutils/properties.h>
#include "sony_audio_bt_radio_owner.h"
#include "sony_audio_bt_driver_close.h"
using android::sp;using android::hardware::Return;using android::hardware::Void;using android::hardware::hidl_vec;
namespace NF=android::hardware::nfc::V1_0;
namespace BT=android::hardware::bluetooth::V1_0;
class Channel {
 int fd_=-1,p_[2]={-1,-1};std::thread t_;std::mutex tx_;std::function<int(int)> close_;
 int closeFd(){int fd=fd_;fd_=-1;if(close_)return close_(fd);return ::close(fd)?-errno:0;}
 public:
 int fd(){return fd_;}
 bool start(const char*path,std::function<void(const uint8_t*,size_t)> rx,std::function<bool(int)> setup={},std::function<int(int)> closer={},bool *opened=nullptr,int *cleanup=nullptr){
  if(opened)*opened=false;if(cleanup)*cleanup=0;
  if(fd_>=0)return false;
  close_=closer;fd_=::open(path,O_RDWR|O_CLOEXEC|O_NONBLOCK|O_NOFOLLOW);if(fd_<0)return false;if(opened)*opened=true;
  if(setup&&!setup(fd_)){int rc=closeFd();if(cleanup)*cleanup=rc;return false;}
  if(pipe2(p_,O_CLOEXEC|O_NONBLOCK)){int rc=closeFd();if(cleanup)*cleanup=rc;return false;}
  t_=std::thread([this,rx]{pollfd p[2]={{fd_,POLLIN,0},{p_[0],POLLIN,0}};uint8_t b[8192];
   while(true){int r=poll(p,2,-1);if(r<0&&errno==EINTR)continue;if(r<=0||p[1].revents)break;
    if(p[0].revents&(POLLERR|POLLHUP|POLLNVAL))break;
    if(p[0].revents&POLLIN){ssize_t n=::read(fd_,b,sizeof(b));if(n>0)rx(b,n);else if(n<0&&errno!=EAGAIN&&errno!=EINTR)break;}
   }});return true;
 }
 unsigned send(const uint8_t*b,size_t n){std::lock_guard<std::mutex>l(tx_);if(fd_<0)return 0;ssize_t r=::write(fd_,b,n);return r>0?r:0;}
 int stop(){if(fd_<0)return 0;char c=1;::write(p_[1],&c,1);if(t_.joinable())t_.join();std::lock_guard<std::mutex>l(tx_);int rc=closeFd();for(int i=0;i<2;++i){if(::close(p_[i])&&!rc)rc=-errno;p_[i]=-1;}return rc;}
 ~Channel(){stop();}
};
class Nfc final:public NF::INfc {
 Channel ch_;sp<NF::INfcClientCallback> cb_;std::mutex life_;std::atomic<bool> routing_{false};
 void shutdown(){if(ch_.fd()>=0)ioctl(ch_.fd(),_IO('S',2),1);ch_.stop();}
 NF::NfcStatus connect(const sp<NF::INfcClientCallback>&cb){
  if(cb==nullptr)return NF::NfcStatus::FAILED;
  shutdown();
  cb_=cb;
  if(!ch_.start("/dev/cxd224x-i2c",[this,cb](const uint8_t*b,size_t n){if(n<3||size_t(b[2])+3!=n)return;if(n==4&&b[0]==0x41&&b[1]==1&&routing_.exchange(false)){(void)cb->sendEvent(NF::NfcEvent::POST_INIT_CPLT,b[3]==0?NF::NfcStatus::OK:NF::NfcStatus::FAILED).isOk();return;}hidl_vec<uint8_t>v;v.setToExternal(const_cast<uint8_t*>(b),n);(void)cb->sendData(v).isOk();})){cb_=nullptr;return NF::NfcStatus::FAILED;}
  if(ioctl(ch_.fd(),_IO('S',2),0)||ioctl(ch_.fd(),_IO('S',3),0)<0){shutdown();cb_=nullptr;return NF::NfcStatus::FAILED;}
  usleep(300000);(void)cb->sendEvent(NF::NfcEvent::OPEN_CPLT,NF::NfcStatus::OK).isOk();return NF::NfcStatus::OK;
 }
 public:
 Return<NF::NfcStatus> open(const sp<NF::INfcClientCallback>&cb)override{std::lock_guard<std::mutex>l(life_);return connect(cb);}
 Return<uint32_t> write(const hidl_vec<uint8_t>&v)override{return ch_.send(v.data(),v.size());}
 Return<NF::NfcStatus> coreInitialized(const hidl_vec<uint8_t>&)override{std::lock_guard<std::mutex>l(life_);if(!cb_)return NF::NfcStatus::FAILED;uint8_t route[]={0x21,1,7,0,1,1,3,0,1,4};routing_=true;if(ch_.send(route,sizeof(route))!=sizeof(route)){routing_=false;return NF::NfcStatus::FAILED;}return NF::NfcStatus::OK;}
 Return<NF::NfcStatus> prediscover()override{return NF::NfcStatus::FAILED;}
 Return<NF::NfcStatus> close()override{std::lock_guard<std::mutex>l(life_);auto cb=cb_;shutdown();cb_=nullptr;if(cb)(void)cb->sendEvent(NF::NfcEvent::CLOSE_CPLT,NF::NfcStatus::OK).isOk();return NF::NfcStatus::OK;}
 Return<NF::NfcStatus> controlGranted()override{if(cb_)(void)cb_->sendEvent(NF::NfcEvent::RELEASE_CONTROL,NF::NfcStatus::OK).isOk();return NF::NfcStatus::OK;}
 Return<NF::NfcStatus> powerCycle()override{std::lock_guard<std::mutex>l(life_);auto cb=cb_;shutdown();cb_=nullptr;return connect(cb);}
};
static bool bluetoothSetup(int fd){
 uint8_t factory[64];int f=::open("/dev/walkdroid-BT_Addr",O_RDONLY|O_CLOEXEC);if(f<0)return false;ssize_t n=::read(f,factory,64);::close(f);if(n!=64)return false;
 auto cmd=[fd](const uint8_t*b,size_t n){if(::write(fd,b,n)!=(ssize_t)n)return false;uint8_t r[512];pollfd p={fd,POLLIN,0};if(poll(&p,1,2500)<=0)return false;ssize_t got=::read(fd,r,sizeof(r));return got>=7&&r[0]==4&&r[1]==14&&r[4]==b[1]&&r[5]==b[2]&&r[6]==0;};
 uint8_t reset[]={1,3,12,0};if(!cmd(reset,sizeof(reset)))return false;
 uint8_t addr[]={1,0x1a,0xfc,6,factory[5],factory[4],factory[3],factory[2],factory[1],factory[0]};return cmd(addr,sizeof(addr));
}
class Bluetooth final:public BT::IBluetoothHci {
 Channel ch_;std::mutex life_;std::vector<uint8_t>pending_;sp<BT::IBluetoothHciCallbacks> cb_;
 sony_bt_radio_owner *radio_=nullptr;std::atomic<bool> sealed_{false};
 bool valid(){if(sealed_.load()||!radio_||sony_bt_radio_check(radio_)){sealed_=true;return false;}return true;}
 int shutdown(){
  int rc=ch_.stop();if(radio_){int finish=sony_bt_radio_finish(&radio_,!rc&&!sealed_.load());if(!rc)rc=finish;}
  if(rc)sealed_=true;pending_.clear();cb_=nullptr;return rc;
 }
 void sendLocked(uint8_t type,const hidl_vec<uint8_t>&v){
  if(!valid()||ch_.fd()<0)return;std::vector<uint8_t>b;b.reserve(v.size()+1);b.push_back(type);b.insert(b.end(),v.begin(),v.end());if(ch_.send(b.data(),b.size())!=b.size()){sealed_=true;ALOGE("ZX9 HCI incomplete write; radio lease sealed");}
 }
 public:
 ~Bluetooth(){shutdown();}
 Return<void> initialize(const sp<BT::IBluetoothHciCallbacks>&cb)override{
  std::lock_guard<std::mutex>l(life_);if(!cb)return Void();
  
  if(shutdown()||sealed_.load()){(void)cb->initializationComplete(BT::Status::INITIALIZATION_ERROR).isOk();return Void();}
  int acquire=sony_bt_radio_acquire(&radio_,SONY_BT_RADIO_ANDROID);if(acquire){ALOGE("ZX9 HCI radio lease denied %d",acquire);(void)cb->initializationComplete(BT::Status::INITIALIZATION_ERROR).isOk();return Void();}
  cb_=cb;bool opened=false;int cleanup=0;bool ok=ch_.start("/dev/stpbt",[this,cb](const uint8_t*b,size_t n){
   if(!valid())return;
   pending_.insert(pending_.end(),b,b+n);
   while(!pending_.empty()){
    uint8_t type=pending_[0];size_t hdr=type==4?2:type==2?4:type==3?3:0;
    if(!hdr){pending_.erase(pending_.begin());continue;}
    if(pending_.size()<hdr+1)return;
    size_t len=type==4?pending_[2]:type==3?pending_[3]:pending_[3]|(size_t(pending_[4])<<8);
    if(pending_.size()<hdr+1+len)return;
    // MTK ROM E1 advertises page 2 but answers page 2 with status 0x30.
    // Preserve all actual feature bits on pages 0/1; only cap advertised range.
    if(type==4&&hdr+1+len==17&&pending_[1]==14&&pending_[4]==4&&pending_[5]==16&&pending_[6]==0&&pending_[8]==2)pending_[8]=1;
    hidl_vec<uint8_t>v;v.setToExternal(pending_.data()+1,hdr+len);
    if(type==4){if(v.size()>=5&&v[0]==14){ALOGI("ZX9 HCI complete opcode=%02x%02x length=%zu status=%02x",v[4],v[3],v.size(),v.size()>5?v[5]:255);if(v[3]==4&&v[4]==16){char x[256]={0};for(size_t i=0;i<v.size()&&i<64;i++)snprintf(x+i*3,4,"%02x ",v[i]);ALOGI("ZX9 HCI extended %s",x);}}(void)cb->hciEventReceived(v).isOk();}else if(type==2)(void)cb->aclDataReceived(v).isOk();else (void)cb->scoDataReceived(v).isOk();
    pending_.erase(pending_.begin(),pending_.begin()+hdr+1+len);
   }
  },bluetoothSetup,sony_bt_driver_close,&opened,&cleanup);
  if(!ok){int finish=sony_bt_radio_finish(&radio_,opened&&!cleanup);if(!opened||cleanup||finish)sealed_=true;cb_=nullptr;pending_.clear();}
  (void)cb->initializationComplete(ok?BT::Status::SUCCESS:BT::Status::INITIALIZATION_ERROR).isOk();return Void();
 }
 Return<void> sendHciCommand(const hidl_vec<uint8_t>&v)override{
  std::lock_guard<std::mutex>l(life_);if(!valid()||ch_.fd()<0)return Void();
  // Android's optional vendor capability ABI is not MTK's vendor opcode ABI.
  // Reject its capability query; do not dispatch a colliding vendor command.
  if(v.size()==3&&v[0]==0x53&&v[1]==0xfd&&v[2]==0){auto cb=cb_;if(cb){uint8_t e[]={0x0e,4,1,0x53,0xfd,1};hidl_vec<uint8_t>r;r.setToExternal(e,sizeof(e));(void)cb->hciEventReceived(r).isOk();}return Void();}
  if(v.size()>=2)ALOGI("ZX9 HCI command %02x%02x length=%zu",v[1],v[0],v.size());sendLocked(1,v);return Void();}
 Return<void> sendAclData(const hidl_vec<uint8_t>&v)override{std::lock_guard<std::mutex>l(life_);sendLocked(2,v);return Void();}
 Return<void> sendScoData(const hidl_vec<uint8_t>&v)override{std::lock_guard<std::mutex>l(life_);sendLocked(3,v);return Void();}
 Return<void> close()override{std::lock_guard<std::mutex>l(life_);int rc=shutdown();ALOGI("ZX9 HCI verified OFF status=%d sealed=%d",rc,(int)sealed_.load());return Void();}
};
int main(){android::hardware::configureRpcThreadpool(8,true);sp<Nfc>n=new Nfc;sp<Bluetooth>b=new Bluetooth;if(n->registerAsService()!=android::OK||b->registerAsService()!=android::OK)return 2;char pid[32];snprintf(pid,sizeof(pid),"%d",getpid());if(property_set("sys.walkdroid.bt_radio_owner_version","1")||property_set("sys.walkdroid.bt_radio_owner_pid",pid))return 3;android::hardware::joinRpcThreadpool();return 0;}
