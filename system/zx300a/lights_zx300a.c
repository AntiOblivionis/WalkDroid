/* Sony physical backlight/charging trigger with event-driven CPU ownership. */
#include "sony_audio_cpu.h"
#include <hardware/lights.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
struct zxlight {struct light_device_t dev;int battery;};
static int set(struct light_device_t *dev,const struct light_state_t *state){
    if(!dev||!state)return -EINVAL;
    struct zxlight *l=(struct zxlight *)dev;
    if(l->battery){
        static const char value[]="battery-charging";
        int fd=open("/sys/class/leds/red/trigger",O_WRONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -errno;
        ssize_t n=write(fd,value,sizeof(value)-1);int rc=n<0?-errno:n!=sizeof(value)-1?-EIO:0;
        if(close(fd)&&!rc)rc=-errno;return rc;
    }
    unsigned color=state->color;
    return sony_audio_cpu_backlight((77*((color>>16)&255)+150*((color>>8)&255)+29*(color&255))>>8);
}
static int close_light(struct hw_device_t *dev){free(dev);return 0;}
static int open_light(const struct hw_module_t *module,const char *name,struct hw_device_t **out){
    if(!name||!out)return -EINVAL;
    if(strcmp(name,LIGHT_ID_BACKLIGHT)&&strcmp(name,LIGHT_ID_BATTERY))return -ENOSYS;
    struct zxlight *l=calloc(1,sizeof(*l));if(!l)return -ENOMEM;
    l->battery=!strcmp(name,LIGHT_ID_BATTERY);l->dev.common.tag=HARDWARE_DEVICE_TAG;
    l->dev.common.version=LIGHTS_DEVICE_API_VERSION_2_0;l->dev.common.module=(struct hw_module_t *)module;
    l->dev.common.close=close_light;l->dev.set_light=set;*out=&l->dev.common;return 0;
}
static struct hw_module_methods_t methods={.open=open_light};
struct hw_module_t HAL_MODULE_INFO_SYM={.tag=HARDWARE_MODULE_TAG,.module_api_version=HARDWARE_MODULE_API_VERSION(1,0),
    .hal_api_version=HARDWARE_HAL_API_VERSION,.id=LIGHTS_HARDWARE_MODULE_ID,
    .name="Sony ZX300A coordinated LCD and charging LED",.author="WalkDroid",.methods=&methods};
