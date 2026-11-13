#include "sony_audio_bt_driver_close.h"
#include <errno.h>
#include <unistd.h>

int sony_bt_driver_close(int fd){
    if(fd<0)return -EINVAL;
    return close(fd)?-errno:0;
}
