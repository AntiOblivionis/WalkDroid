#include <android/hardware/configstore/1.1/ISurfaceFlingerConfigs.h>
#include <hidl/HidlTransportSupport.h>
#include "SurfaceFlingerConfigs.h"
int main() {
 android::hardware::configureRpcThreadpool(4,true);
 android::sp<android::hardware::configstore::V1_1::ISurfaceFlingerConfigs> service=
   new android::hardware::configstore::V1_1::implementation::SurfaceFlingerConfigs;
 if(service->registerAsService()!=android::OK)return 1;
 android::hardware::joinRpcThreadpool();return 0;
}
