/* Framework factory wrapping the system AudioPolicyManager. No application
 * is automatically selected. */
#include "sony_audio_app_policy_adapter.h"
#include "sony_audio_policy_control.h"
#include <dlfcn.h>
#include <string.h>
#include <log/log.h>
using namespace android;
static SonyPolicyAdapter *current=nullptr;
static AudioPolicyInterface *interface=nullptr;
static void *originalLibrary=nullptr;
static SonyPolicyCreate originalCreate=nullptr;
static SonyPolicyDestroy originalDestroy=nullptr;
static int factoryError=0;
static int controlError=0;
#define WD_FACTORY_EXPORT extern "C" __attribute__((visibility("default")))
static bool loadOriginal(){
    if(originalLibrary&&originalCreate&&originalDestroy)return true;
    originalLibrary=dlopen("libaudiopolicymanager.so",RTLD_NOW|RTLD_LOCAL);
    if(!originalLibrary)return false;
    originalCreate=(SonyPolicyCreate)dlsym(originalLibrary,"createAudioPolicyManager");
    originalDestroy=(SonyPolicyDestroy)dlsym(originalLibrary,"destroyAudioPolicyManager");
    return originalCreate&&originalDestroy;
}
WD_FACTORY_EXPORT AudioPolicyInterface *createAudioPolicyManager(AudioPolicyClientInterface *client){
    if(current||interface||factoryError||!client)return nullptr;
    if(!loadOriginal()){
        factoryError=NO_INIT;__android_log_print(ANDROID_LOG_ERROR,"WalkDroidPolicy","Original factory unavailable: %s",dlerror());return nullptr;
    }
    status_t rc=sony_policy_adapter_create(originalCreate,originalDestroy,client,&current,&interface);
    if(rc){factoryError=rc;__android_log_print(ANDROID_LOG_ERROR,"WalkDroidPolicy","Factory initialization failed: %d; exact object retained",rc);return nullptr;}
    controlError=sony_policy_control_publish(current);
    if(controlError)__android_log_print(ANDROID_LOG_ERROR,"WalkDroidPolicy","Control endpoint unavailable: %d",controlError);
    return interface;
}
WD_FACTORY_EXPORT void destroyAudioPolicyManager(AudioPolicyInterface *p){
    if(!p||p!=interface||!current){factoryError=INVALID_OPERATION;return;}
    status_t rc=sony_policy_control_disable(current);
    if(!rc)rc=sony_policy_adapter_destroy(&current,&interface);
    if(rc){factoryError=rc;__android_log_print(ANDROID_LOG_ERROR,"WalkDroidPolicy","Factory retirement refused: %d; object retained",rc);}
    /* The original DSO remains loaded for process lifetime. No implicit reset,
     * dlclose of retained original callbacks or retry after uncertainty. */
}
/* In-process integration API only. The caller must already hold the owning
 * AudioPolicyService lock and resolve the selected package.
 * This is NOT exposed through AudioSystem.setParameters or a Binder service. */
WD_FACTORY_EXPORT status_t walkdroid_policy_factory_bringup_select_uid(int32_t uid){
    if(factoryError||!current)return NO_INIT;return sony_policy_adapter_select_uid(current,uid);
}
WD_FACTORY_EXPORT status_t walkdroid_policy_factory_state(SonyPolicyAdapterState *out){
    if(factoryError||!current)return NO_INIT;return sony_policy_adapter_state(current,out);
}
