#ifndef SONY_CFW_WORKER_CONTEXT_H
#define SONY_CFW_WORKER_CONTEXT_H
/* Optional final argv: --cfw-context <unsigned signature> <unsigned ship>.
 * Consumes only the tail. Unrecognized values are resolved by the original
 * provider. No file, NVP, Android property or AI IPC write is performed.
 * Return -1 reports malformed arguments; 0 leaves the context unbound;
 * 1 binds it for this process lifetime. */
int sony_cfw_worker_context_args(int *argc,char **argv);
#endif
