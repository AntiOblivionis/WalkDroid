#ifndef SONY_AUDIO_USB_INPUT_POLICY_H
#define SONY_AUDIO_USB_INPUT_POLICY_H
#include <stdint.h>
/* Values executed from this unit's original Core::OpenInhal and Inhal::Open.
 * This policy controls parameters only, not a user-space clock or resampler. */
enum sony_usb_input_resource {SONY_USB_INPUT_F_START,SONY_USB_INPUT_F_THRESH,SONY_USB_INPUT_F_PLUS,SONY_USB_INPUT_F_MINUS,SONY_USB_INPUT_RESOURCE_COUNT};
struct sony_usb_input_policy;
struct sony_usb_input_policy_io {
    int (*check)(void *); /* Retained for API compatibility; not invoked. */
    int (*read)(void *,enum sony_usb_input_resource,unsigned *);
    int (*write)(void *,enum sony_usb_input_resource,unsigned);
    int (*capture_closed)(void *); /* Retained for API compatibility; not invoked. */
    void *context;
};
struct sony_usb_input_policy_state {
    uint64_t receipt;
    unsigned before[SONY_USB_INPUT_RESOURCE_COUNT],desired[SONY_USB_INPUT_RESOURCE_COUNT];
    unsigned snapshot_valid,prepare_attempted,prepared,feedback_attempted,feedback_ready;
    unsigned acquired_mask,confirmed_mask,restore_attempted_mask,restored_mask,release_attempted,released;
    int first_error,restore_error;
};
/* Allocate without writing. The caller owns the outer USB resources. */
int sony_usb_input_policy_create(struct sony_usb_input_policy **,const struct sony_usb_input_policy_io *,uint64_t receipt);
/* Save original parameters and request the configured start value. */
int sony_usb_input_policy_prepare(struct sony_usb_input_policy *);
/* Request feedback parameters; prefill_verified remains API metadata. */
int sony_usb_input_policy_feedback(struct sony_usb_input_policy *,int prefill_verified);
/* Retained compatibility entry point; it performs no hardware verification. */
int sony_usb_input_policy_check(struct sony_usb_input_policy *);
int sony_usb_input_policy_state(struct sony_usb_input_policy *,struct sony_usb_input_policy_state *);
/* Attempt to restore saved values in reverse order; errors can be retried.
 * confirmed_mask/restored_mask report successful writes, not physical readback. */
int sony_usb_input_policy_release(struct sony_usb_input_policy *);
int sony_usb_input_policy_destroy(struct sony_usb_input_policy **);
#endif
