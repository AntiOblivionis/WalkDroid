#ifndef WALKDROID_SONY_PLAYER_PROTOCOL_H
#define WALKDROID_SONY_PLAYER_PROTOCOL_H
#include <stdint.h>
#include <sys/types.h>
#define WDS_PLAYER_MAGIC 0x50534457u
#define WDS_PLAYER_VERSION 1u
#define WDS_PLAYER_SOCKET "walkdroid.sony.player.v1"
#define WDS_PLAYER_MAX_FILE (8ull*1024*1024*1024)
enum wds_player_command { WDS_PLAYER_STATUS=1,WDS_PLAYER_START=2,WDS_PLAYER_STOP=3 };
enum wds_player_phase { WDS_PLAYER_IDLE=0,WDS_PLAYER_STARTING=1,WDS_PLAYER_PLAYING=2,WDS_PLAYER_STOPPING=3,WDS_PLAYER_ENDED=4,WDS_PLAYER_STOPPED=5,WDS_PLAYER_FAILED=6 };
enum wds_player_flags {WDS_PLAYER_CHILDREN_CLEAN=1u,WDS_PLAYER_REFUSED=4u,WDS_PLAYER_ATTEMPTED=8u,WDS_PLAYER_NATIVE_DSD=16u};
struct wds_player_request {uint32_t magic,version,command,reserved;uint64_t request,session;};
struct wds_player_reply {
 uint32_t magic,version,phase;int32_t error;
 /* Native flag: output bytes are stereo DSD bytes, rate is DSD bit rate;
  * otherwise stereo S32LE bytes and rate is PCM frames/second. These counters
  * confirm sink admission, not physical consumption or exact music position. */
 uint64_t request,session,source_bytes,output_acked_bytes;
 uint32_t rate,source_finished;int32_t cleanup_error;uint32_t retained;
 uint64_t generation;uint32_t revision,flags;
};
/* Seqpacket/SCM_RIGHTS intake. Short messages are zero-filled; the first FD is
 * returned and any others are closed. Legacy uid and request number are unused. */
int wds_player_receive(int socket,uid_t uid,uint64_t last_request,struct wds_player_request *,int *file);
#endif
