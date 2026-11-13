#ifndef WALKDROID_SONY_VOLUME_POLICY_H
#define WALKDROID_SONY_VOLUME_POLICY_H
/* Pure successful-operation policy, transcribed from hash-pinned original ARM
 * VolumeAdlerOut. No device access, persistence, ALSA, or speculative commit.
 * The owner must apply hardware/config atomically and publish next only after
 * successful readback. Never identify last_headphone with the current jack.
 * This module alone is NOT the complete native VolumeService integration.
 */
enum sony_volume_command {
    SONY_VOLUME_OUTPUT=0, SONY_VOLUME_SE_PLUG=1, SONY_VOLUME_BTL_PLUG=2,
    SONY_VOLUME_SE_GAIN=3, SONY_VOLUME_BTL_GAIN=4
};
struct sony_volume_state {
    unsigned int ship_selector;
    int volume,output,last_headphone,se_connected,btl_connected;
    int se_gain,btl_gain,avls,avls_threshold;
};
struct sony_volume_plan {
    struct sony_volume_state next;
    int set_volume,requested_volume,adjust_target,update_avls;
};
int sony_volume_initial(unsigned int ship,int output,int gain);
int sony_volume_plan_reconfigure(const struct sony_volume_state *,int command,struct sony_volume_plan *);
int sony_volume_plan_set(const struct sony_volume_state *,int volume,struct sony_volume_plan *);
int sony_volume_plan_avls(const struct sony_volume_state *,int enabled,struct sony_volume_plan *);
#endif
