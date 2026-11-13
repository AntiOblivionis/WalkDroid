#ifndef SONY_AUDIO_BT_PEER_CONFIG_H
#define SONY_AUDIO_BT_PEER_CONFIG_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A peer is supplied for this session; it is never compiled into a worker.
 * A chroot may provide /data/walkdroid-peer instead of an environment value. */
static int sony_bt_configured_peer(unsigned char peer[6]) {
    char line[80];const char *value=getenv("WALKDROID_BT_PEER");
    if(!value||!*value){
        FILE *f=fopen("/data/walkdroid-peer","r");
        if(!f)return -1;
        value=fgets(line,sizeof(line),f);fclose(f);
        if(!value)return -1;
    }
    unsigned parts[6];char trailing;
    if(sscanf(value," %2x:%2x:%2x:%2x:%2x:%2x %c",&parts[0],&parts[1],&parts[2],&parts[3],&parts[4],&parts[5],&trailing)!=6)return -1;
    for(unsigned i=0;i<6;i++)peer[i]=(unsigned char)parts[i];
    return 0;
}

#endif
