#ifndef WALKDROID_SONY_SETTINGS_RELAY_H
#define WALKDROID_SONY_SETTINGS_RELAY_H
#include "sony_audio_owner_commands.h"
/* Strict external-DSF settings API. No local-PCM fallback and no hardware IO.
 * A 32 lowercase-hex request id correlates UI polling; it is not app authority.
 * Exact header prefix, generation, revision and at least one whitelisted field
 * are mandatory. Unsupported hardware/format fields reject before connecting. */
struct sony_settings_patch {
    uint64_t generation;uint32_t revision,mask;char id[33];
    struct sony_dsd_live_controls values;
};
int sony_settings_patch_parse(const char *,struct sony_settings_patch *);
int sony_settings_patch_apply(const struct sony_settings_patch *,const struct wdf_controls *,struct sony_dsd_live_controls *);
struct sony_settings_relay;
enum sony_settings_phase {SONY_SETTINGS_CHECKING=1,SONY_SETTINGS_APPLYING=2,
    SONY_SETTINGS_APPLIED=3,SONY_SETTINGS_REJECTED=4,SONY_SETTINGS_FAILED=5,SONY_SETTINGS_UNKNOWN=6};
/* One controller serializes start/poll/result/close. Timeout 10..10000 ms.
 * Socket operations do not block; controller must poll while busy, including
 * when no Android PCM writes arrive. Initial failure leaves *out NULL; after
 * start, errors are retained as result, never retried or routed to another owner.
 * _at uses the protected stable-directory contract of owner_connect_at. */
int sony_settings_relay_start(struct sony_settings_relay **,const char *,unsigned timeout_ms);
int sony_settings_relay_start_at(struct sony_settings_relay **,int,const char *,unsigned timeout_ms);
/* Explicit kind from the freshly observed owner. USB_DAC admits volume only;
 * legacy start/_at retain their DSF-only contract. Never probe another kind. */
int sony_settings_relay_start_kind(struct sony_settings_relay **,enum sony_audio_owner_kind,const char *,unsigned timeout_ms);
int sony_settings_relay_start_at_kind(struct sony_settings_relay **,int,enum sony_audio_owner_kind,const char *,unsigned timeout_ms);
int sony_settings_relay_busy(const struct sony_settings_relay *);
int sony_settings_relay_matches(const struct sony_settings_relay *,const char *id);
int sony_settings_relay_poll(struct sony_settings_relay *);
/* Query must exactly match the UI id, including terminal/error states. Retained
 * receipts are historical confirmations, never a replacement for sony_state. */
char *sony_settings_relay_result(const struct sony_settings_relay *,const char *id);
void sony_settings_relay_close(struct sony_settings_relay **);
#endif
