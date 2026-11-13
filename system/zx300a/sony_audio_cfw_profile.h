#ifndef WALKDROID_SONY_CFW_PROFILE_H
#define WALKDROID_SONY_CFW_PROFILE_H
/* CFW set_mode maps these to untouched, packaged Adler variants. */
static inline int sony_cfw_profile_valid(int mode) {return mode>=0&&mode<=2;}
static inline const char *sony_cfw_profile_library(int mode) {
    return mode==0?"/opt/walkdroid/profiles/adler/normal/libaudiohal-adleralsa.so":
           mode==1?"/opt/walkdroid/profiles/adler/pv1/libaudiohal-adleralsa.so":
           mode==2?"/opt/walkdroid/profiles/adler/pv2/libaudiohal-adleralsa.so":(const char *)0;
}
/* Separate private Android store; no original NVP bytes are repurposed.
 * An absent file uses the historical B59 Plus-v2 default. The expected value
 * is retained for API compatibility and does not gate a commit; -117 means
 * rename succeeded but directory durability was not confirmed. */
int sony_cfw_profile_read(const char *directory,int *mode);
int sony_cfw_profile_commit(const char *directory,int expected,int mode);
/* GMD is applied during boot, separately from the current playback profile.
 * Missing gain selection means the original normal table (0). */
int sony_cfw_gain_read(const char *directory,int *mode);
int sony_cfw_gain_commit(const char *directory,int expected,int mode);
/* Separate private signature selection. -1 means legacy/unselected; 0..3
 * names the audited logical contexts, not a claim of four different EQs.
 * An absent file reads -1. */
int sony_cfw_signature_read(const char *directory,int *signature);
int sony_cfw_signature_commit(const char *directory,int expected,int signature);
#endif
