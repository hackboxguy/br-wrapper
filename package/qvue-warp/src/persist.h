#ifndef QVUE_WARPD_PERSIST_H
#define QVUE_WARPD_PERSIST_H

#include <limits.h>
#include <stddef.h>

int write_atomic(const char *path, const char *data, size_t len, char *err, size_t errlen);
// the writable warp.conf layer: set keys in place, keep the rest of the file
int conf_set_keys(const char *path, const char *const keys[], const char *const vals[], int n, char *err,
                  size_t errlen);
int conf_set_enabled(const char *path, int enabled, char *err, size_t errlen);
int pick_run_dir(char *out, size_t outlen);

#endif
