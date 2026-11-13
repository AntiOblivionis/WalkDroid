#include "sony_audio_jack.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#define GPIO_QUERY_INPUT _IOR(0x90,0x12,uint32_t)
int sony_audio_jack_read(struct sony_audio_jack_state *state) {
    if(!state)return -EINVAL;
    int fd=open("/dev/mtgpio",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -ENODEV;
    const unsigned long pins[]={53,42,39};int inputs[3],rc=0;
    for(unsigned int i=0;i<3&&!rc;++i){int v=ioctl(fd,GPIO_QUERY_INPUT,pins[i]);if(v<0)rc=-errno;else inputs[i]=!!v;}
    close(fd);if(rc)return rc;
    /* ICX1288 TYPE_Z / BTL_HEADPHONE_PLUG44_FLAG: SE is active-low, but the
     * native BTL branch inverts its already inverted low bit => GPIO42 high.
     * GPIO39 is sampled/validated, but is not a presence bit for this board. */
    state->se=1-inputs[0];state->btl=inputs[1];return 0;
}
