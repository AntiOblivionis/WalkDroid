#include "sony_audio_sound_presets.h"
#include "sony_audio_snapshot_store.h"
#include <errno.h>
#include <stdint.h>
#include <string.h>
struct sound_field {unsigned int current,first,stride;};
/* Actual original FilterInDmp SaveUserPreset config calls: 27 writes, 26
 * distinct cells (SS_EQ_USE is written by both EQ10 and tone). Mapping is from
 * original FindNvpKey/driver offsets, not guessed from preset labels.
 * Evidence: sound-presets-host/20261029T065823078944Z (EQ) and
 * 20261029T070052174638Z (tone), under research/sony-audio.
 */
static const struct sound_field fields[]={
    {16432u,16648u,216u}, /* SS_DSEECUST_ONOFF */
    {16436u,16652u,216u}, /* SS_DSEECUST_MODE */
    {16448u,16664u,216u}, /* SS_DN_ONOFF */
    {16532u,16748u,216u}, /* SS_EQ10_ONOFF */
    {16540u,16756u,216u}, /* SS_EQ10_BAND01 */
    {16544u,16760u,216u}, /* SS_EQ10_BAND02 */
    {16548u,16764u,216u}, /* SS_EQ10_BAND03 */
    {16552u,16768u,216u}, /* SS_EQ10_BAND04 */
    {16556u,16772u,216u}, /* SS_EQ10_BAND05 */
    {16560u,16776u,216u}, /* SS_EQ10_BAND06 */
    {16564u,16780u,216u}, /* SS_EQ10_BAND07 */
    {16568u,16784u,216u}, /* SS_EQ10_BAND08 */
    {16572u,16788u,216u}, /* SS_EQ10_BAND09 */
    {16576u,16792u,216u}, /* SS_EQ10_BAND10 */
    {16580u,16796u,216u}, /* SS_TONE_ONOFF */
    {16584u,16800u,216u}, /* SS_TONE_LOW */
    {16588u,16804u,216u}, /* SS_TONE_MID */
    {16592u,16808u,216u}, /* SS_TONE_HIGH */
    {16596u,16812u,216u}, /* SS_TONE_LOW_FREQ */
    {16600u,16816u,216u}, /* SS_TONE_MID_FREQ */
    {16604u,16820u,216u}, /* SS_TONE_HIGH_FREQ */
    {16608u,16824u,216u}, /* SS_EQ_USE */
    {16612u,16828u,216u}, /* SS_DCLINEAR_ONOFF */
    {16616u,16832u,216u}, /* SS_DCLINEAR_FILTER */
    {17416u,17428u,12u}, /* VINYL_ONOFF */
    {17420u,17432u,12u}, /* VINYL_TYPE */
};
static uint32_t word(const unsigned char *p){
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static int input_valid(const void *snapshot,unsigned int size,unsigned int slot){
    if(!snapshot||size!=SONY_SNAPSHOT_BYTES||slot<1||slot>3)return -EINVAL;
    return sony_snapshot_validate(snapshot);
}
static unsigned int bank_offset(unsigned int index,unsigned int slot){
    return fields[index].first+(slot-1)*fields[index].stride;
}
int sony_sound_preset_available(const void *snapshot,unsigned int size,unsigned int slot){
    int rc=input_valid(snapshot,size,slot);if(rc)return rc;
    const unsigned char *source=snapshot;
    for(unsigned int i=0;i<SONY_SOUND_PRESET_FIELDS;++i)
        if(word(source+bank_offset(i,slot))==0xfefeu)return -ENODATA;
    return 0;
}
int sony_sound_preset_project(const void *snapshot,unsigned int size,
    unsigned int operation,unsigned int slot,void *output){
    if(!output||(operation!=SONY_SOUND_PRESET_SAVE&&operation!=SONY_SOUND_PRESET_LOAD))return -EINVAL;
    int rc=input_valid(snapshot,size,slot);if(rc)return rc;
    unsigned char staged[SONY_SNAPSHOT_BYTES];memcpy(staged,snapshot,size);
    const unsigned char *source=snapshot;
    for(unsigned int i=0;i<SONY_SOUND_PRESET_FIELDS;++i){
        unsigned int from=operation==SONY_SOUND_PRESET_SAVE?fields[i].current:bank_offset(i,slot);
        unsigned int to=operation==SONY_SOUND_PRESET_SAVE?bank_offset(i,slot):fields[i].current;
        if(word(source+from)==0xfefeu)return -ENODATA;
        memcpy(staged+to,source+from,4);
    }
    uint32_t crc=~0u;
    for(unsigned int i=0;i<size;++i){
        crc^=(i>=4096&&i<4216)?0:staged[i];
        for(unsigned int bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));
    }
    crc=~crc;for(unsigned int i=0;i<4;++i)staged[4096+i]=(unsigned char)(crc>>(8*i));
    memcpy(output,staged,size);return 0;
}
