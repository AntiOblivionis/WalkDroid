// AOSP Pie standard passthrough registration, audio 2.0 subset for this device.
#include <android/hardware/audio/2.0/IDevicesFactory.h>
#include <android/hardware/audio/effect/2.0/IEffectsFactory.h>
#include <hidl/LegacySupport.h>
#include <hidl/HidlTransportSupport.h>
#include <binder/ProcessState.h>
int main(){
 using namespace android::hardware;
 android::ProcessState::initWithDriver("/dev/vndbinder");
 android::ProcessState::self()->startThreadPool();
 configureRpcThreadpool(16,true);
 if(registerPassthroughServiceImplementation<audio::V2_0::IDevicesFactory>()!=android::OK)return 1;
 if(registerPassthroughServiceImplementation<audio::effect::V2_0::IEffectsFactory>()!=android::OK)return 2;
 joinRpcThreadpool();return 3;
}
