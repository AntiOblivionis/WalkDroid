#ifndef WALKDROID_SONY_DMPCONFIG_FORWARD_H
#define WALKDROID_SONY_DMPCONFIG_FORWARD_H
#include <dlfcn.h>

/* The provider can be a local dependency of a vendor library. Retain a
 * reference only when it was already loaded; never load a new service. */
static void *sony_dmpconfig_original(const char *symbol) {
    void *original=dlsym(RTLD_NEXT,symbol);
    if(!original) {
        static void *provider;
        if(!provider)provider=dlopen("libDmpConfig.so",RTLD_NOW|RTLD_NOLOAD);
        if(provider)original=dlsym(provider,symbol);
    }
    return original;
}
#endif
