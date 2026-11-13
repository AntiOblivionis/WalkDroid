#ifndef WALKDROID_SONY_VOLUME_MEMORY_H
#define WALKDROID_SONY_VOLUME_MEMORY_H
/* Original NVP snapshot fields only. No live plug/threshold state is stored. */
struct sony_volume_memory {int avls,last_headphone;};
static inline int sony_volume_memory_valid(const struct sony_volume_memory *m) {
    return m&&(m->avls==0||m->avls==1)&&(m->last_headphone==1||m->last_headphone==2||m->last_headphone==6);
}
#endif
