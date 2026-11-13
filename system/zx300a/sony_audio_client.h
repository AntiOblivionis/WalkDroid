#ifndef WALKDROID_SONY_AUDIO_CLIENT_H
#define WALKDROID_SONY_AUDIO_CLIENT_H
#include "sony_audio_eq_bridge.h" /* Plain settings struct only; no glibc ABI. */
struct wda_client;
#define WDA_CLIENT_IO -100
#define WDA_CLIENT_TIMEOUT -101
#define WDA_CLIENT_BAD_REPLY -102

/* Consumes both exclusive pipe/socket descriptors on success; caller retains
 * them on failure. Sets O_NONBLOCK. Original fd flags must not be shared with
 * other users. Calls are serialized internally. No process/device selection.
 */
int wda_client_attach(struct wda_client **, int request_fd, int response_fd, int timeout_ms);
/* Returns ACKNOWLEDGED/SAVED EQ requests, not actual hardware/DSP measurements. */
int wda_client_get_saved(struct wda_client *, struct sony_eq_settings *);
int wda_client_get_controls(struct wda_client *, struct sony_eq_controls *);
/* settings=NULL restores previously read saved revision. No automatic retries. */
int wda_client_open_stream(struct wda_client *, const struct sony_eq_settings *settings);
int wda_client_update(struct wda_client *, const struct sony_eq_settings *);
int wda_client_process(struct wda_client *, const float *, float *, unsigned int frames);
int wda_client_close_stream(struct wda_client *);
int wda_client_failed(struct wda_client *);
void wda_client_destroy(struct wda_client *);
#endif
