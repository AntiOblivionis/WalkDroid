#ifndef SONY_AUDIO_USB_GADGET_POLICY_H
#define SONY_AUDIO_USB_GADGET_POLICY_H
#include <stdint.h>
#define SONY_USB_GADGET_TEXT_BYTES 256u
enum sony_usb_gadget_attr {
    SONY_USB_GADGET_ENABLE, SONY_USB_GADGET_FUNCTIONS,
    SONY_USB_GADGET_VENDOR, SONY_USB_GADGET_PRODUCT, SONY_USB_GADGET_SERIAL,
    SONY_USB_GADGET_MANUFACTURER, SONY_USB_GADGET_DESCRIPTION,
    SONY_USB_GADGET_PLUS, SONY_USB_GADGET_MINUS, SONY_USB_GADGET_THRESHOLD,
    SONY_USB_GADGET_START, SONY_USB_GADGET_VALID, SONY_USB_GADGET_ALLOW,
    SONY_USB_GADGET_ATTR_COUNT
};
struct sony_usb_gadget_snapshot {char value[SONY_USB_GADGET_ATTR_COUNT][SONY_USB_GADGET_TEXT_BYTES];};
struct sony_usb_gadget_io {
    /* Backend for sysfs attributes. Positive statuses are treated as errors. */
    int (*check)(void *); /* Retained for API compatibility; not invoked. */
    int (*read)(void *,enum sony_usb_gadget_attr,char *,unsigned capacity);
    int (*write)(void *,enum sony_usb_gadget_attr,const char *);
    int (*quiesced)(void *); /* Retained for API compatibility; not invoked. */
    int (*uac_absent)(void *); /* Retained for API compatibility; not invoked. */
    void *context;
};
struct sony_usb_gadget_state {
    uint64_t receipt;
    struct sony_usb_gadget_snapshot before;
    unsigned prepare_attempted,snapshot_valid,enter_attempted,active;
    unsigned writes_attempted,writes_confirmed,pending_write;
    unsigned restore_started,restore_stage,restored;
    int first_error,restore_error;
};
struct sony_usb_gadget_policy;
/* No I/O or mutation at create. The caller retains the backend. */
int sony_usb_gadget_policy_create(struct sony_usb_gadget_policy **,const struct sony_usb_gadget_io *,uint64_t receipt);
int sony_usb_gadget_policy_prepare(struct sony_usb_gadget_policy *);
/* Save current enable/functions and request disable -> audio_func,ffs -> enable. */
int sony_usb_gadget_policy_enter(struct sony_usb_gadget_policy *);
/* Compatibility entry point; it performs no hardware verification. */
int sony_usb_gadget_policy_check(struct sony_usb_gadget_policy *);
/* Restore the saved enable/functions values. Errors can be retried. The write
 * counters report calls, not independent confirmation of hardware state. */
int sony_usb_gadget_policy_restore(struct sony_usb_gadget_policy *);
int sony_usb_gadget_policy_state(struct sony_usb_gadget_policy *,struct sony_usb_gadget_state *);
int sony_usb_gadget_policy_destroy(struct sony_usb_gadget_policy **);
#endif
