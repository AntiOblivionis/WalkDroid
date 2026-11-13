/* HOST TEST ONLY. Executable property fixture; never build into client DSO. */
#include <string.h>
int __system_property_get(const char *name,char *value) {
    const char *result=!strcmp(name,"ro.serialno")?"TEST-PLAYER-ANDROID":
        !strcmp(name,"ro.product.model")?"NW-ZX300A":!strcmp(name,"ro.board.platform")?"mt8590":"";
    unsigned int n=(unsigned int)strlen(result);memcpy(value,result,n+1);return (int)n;
}
