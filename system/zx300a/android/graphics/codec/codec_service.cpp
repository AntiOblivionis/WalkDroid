// Uses the unmodified AOSP Pie OMX implementation already shipped in Lineage.
#include <binder/ProcessState.h>
#include <hidl/HidlTransportSupport.h>
#include <media/stagefright/omx/1.0/Omx.h>
#include <media/stagefright/omx/1.0/OmxStore.h>
#include <signal.h>
#include <stdio.h>
int main(){
 signal(SIGPIPE,SIG_IGN);
 android::ProcessState::initWithDriver("/dev/vndbinder");
 android::ProcessState::self()->startThreadPool();
 android::hardware::configureRpcThreadpool(8,true);
 using namespace android::hardware::media::omx::V1_0;
 android::sp<IOmxStore> store=new implementation::OmxStore();
 android::sp<IOmx> omx=new implementation::Omx();
 int a=store->registerAsService(),b=omx->registerAsService();
 printf("ZX9_REAL_SOFTWARE_OMX store=%d omx=%d\n",a,b);fflush(stdout);
 if(a||b)return 1;
 android::hardware::joinRpcThreadpool();return 2;
}
