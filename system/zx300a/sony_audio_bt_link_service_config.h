#ifndef WALKDROID_SONY_BT_LINK_SERVICE_CONFIG_H
#define WALKDROID_SONY_BT_LINK_SERVICE_CONFIG_H
#define WLC_PROFILE "production"
#define WLC_STORE "/data/sony-bt-pair-store-v1"
#define WLC_TEMPLATE "/data/walkdroid/bt-receiver/peer-template"
#define WLC_META_SOCKET "walkdroid.sony.bt.catalog.v1"
#define WLC_CONTROL_SOCKET "walkdroid.sony.bt.control.v2"
#define WLC_DATA_SOCKET "walkdroid.sony.bt.data.v1"
#define WLC_RAM_PARENT "/dev"
#define WLC_RAM_NAME "walkdroid-bt-tx"
#define WLC_RAM_PATH "/dev/walkdroid-bt-tx"
#define WLC_SERVICE_EXE "/sony/bt-link/sony-bt-linkd"
#define WLC_GUARD_CGROUP 1
#define WLC_TEST_MODEL 0
#define WLC_LAB 0
#define WLC_MODEL_LEASE "/dev/walkdroid-bt-model"
#define WLC_HAL_SUPPORT 0
#define WLC_HAL_LIBRARY "/sony/bt-link/audio.a2dp.default.so"
#endif
