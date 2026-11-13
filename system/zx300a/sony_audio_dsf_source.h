#ifndef SONY_AUDIO_DSF_SOURCE_H
#define SONY_AUDIO_DSF_SOURCE_H
#include <stddef.h>
#include <stdint.h>
#define SONY_DSF_SOURCE_MAX_READ 32768u
struct sony_dsf_source;
/* Open a readable source relative to a borrowed directory FD. Symbolic links,
 * including /proc/self/fd entries, are accepted. The legacy size-budget
 * argument is retained for callers but is not an admission limit. No copy or
 * file lease is made: an external writer may change the content while playing.
 * The size query reports the current file size. Synchronous I/O may block. */
int sony_dsf_source_open_at(struct sony_dsf_source **out,int directory_fd,const char *basename,uint64_t max_file_bytes);
/* Single API owner; concurrent read/info/close is rejected. Reads are chunked,
 * return the actual length at EOF and report filesystem errors. On an error or
 * cancellation, a prefix of the caller buffer may already have been written. */
int sony_dsf_source_read_at(void *,uint64_t,uint8_t *,size_t,size_t *actual);
int sony_dsf_source_size(struct sony_dsf_source *,uint64_t *);
/* May be requested from another thread; it cannot cancel an in-flight syscall. */
void sony_dsf_source_cancel(struct sony_dsf_source *);
/* Release the descriptor; caller must stop concurrent cancel calls first. */
int sony_dsf_source_close(struct sony_dsf_source **);
#endif
