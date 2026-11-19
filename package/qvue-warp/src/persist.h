#ifndef QVUE_WARPD_PERSIST_H
#define QVUE_WARPD_PERSIST_H

#include <limits.h>
#include <stddef.h>

int write_atomic(const char *path, const char *data, size_t len, char *err, size_t errlen);
int conf_set_enabled(const char *path, int enabled, char *err, size_t errlen);
int pick_run_dir(char *out, size_t outlen);

#endif
