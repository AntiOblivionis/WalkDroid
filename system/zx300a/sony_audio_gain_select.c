/* Read the private next-boot GMD selection; stdout is one verified digit. */
#include "sony_audio_cfw_profile.h"
#include <stdio.h>
int main(int argc,char **argv) {
 if(argc!=2)return 64;
 
 int value=-1,rc=sony_cfw_gain_read(argv[1],&value);
 if(rc){fprintf(stderr,"GMD selection rejected: %d\n",rc);return 1;}
 printf("%d\n",value);return 0;
}
