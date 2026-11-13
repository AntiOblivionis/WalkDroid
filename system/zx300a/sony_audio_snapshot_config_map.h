/* Experimental PCM-chain allowlist, audited native FindNvpKey mapping.
 * Not authorization for hardware routes or all 248 candidate audio keys. */
static const struct config_mapping config_map[]={
    {3u,16396u}, /* SS_BUILTIN_DSDPROCESS */
    {5u,16404u}, /* SS_DSDCONV_GAIN */
    {6u,16408u}, /* SS_DSDCONV_FILTER */
    {7u,16412u}, /* SS_HPMODEL */
    {12u,16432u}, /* SS_DSEECUST_ONOFF */
    {13u,16436u}, /* SS_DSEECUST_MODE */
    {16u,16448u}, /* SS_DN_ONOFF */
    {37u,16532u}, /* SS_EQ10_ONOFF */
    {39u,16540u}, /* SS_EQ10_BAND01 */
    {40u,16544u}, /* SS_EQ10_BAND02 */
    {41u,16548u}, /* SS_EQ10_BAND03 */
    {42u,16552u}, /* SS_EQ10_BAND04 */
    {43u,16556u}, /* SS_EQ10_BAND05 */
    {44u,16560u}, /* SS_EQ10_BAND06 */
    {45u,16564u}, /* SS_EQ10_BAND07 */
    {46u,16568u}, /* SS_EQ10_BAND08 */
    {47u,16572u}, /* SS_EQ10_BAND09 */
    {48u,16576u}, /* SS_EQ10_BAND10 */
    {49u,16580u}, /* SS_TONE_ONOFF */
    {50u,16584u}, /* SS_TONE_LOW */
    {51u,16588u}, /* SS_TONE_MID */
    {52u,16592u}, /* SS_TONE_HIGH */
    {53u,16596u}, /* SS_TONE_LOW_FREQ */
    {54u,16600u}, /* SS_TONE_MID_FREQ */
    {55u,16604u}, /* SS_TONE_HIGH_FREQ */
    {56u,16608u}, /* SS_EQ_USE */
    {57u,16612u}, /* SS_DCLINEAR_ONOFF */
    {58u,16616u}, /* SS_DCLINEAR_FILTER */
    {60u,16624u}, /* SS_SRCDIRECT_ONOFF */
    {61u,16628u}, /* SS_BTEFFECT_ONOFF */
    {252u,17416u}, /* VINYL_ONOFF */
    {253u,17420u}, /* VINYL_TYPE */
};
