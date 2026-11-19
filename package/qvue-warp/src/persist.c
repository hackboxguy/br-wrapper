// Files written by the daemon: warp.conf (enabled, mirror_x, mirror_y), state.json. Atomic
// (temp file + fsync + rename).
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "persist.h"

int write_atomic(const char *path, const char *data, size_t len, char *err, size_t errlen) {
    char tmp[PATH_MAX + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) { snprintf(err, errlen, "%s: %m", tmp); return -1; }
    int bad = fwrite(data, 1, len, f) != len || fflush(f) || fsync(fileno(f));
    if (fclose(f)) bad = 1;
    if (bad) { snprintf(err, errlen, "%s: write failed: %m", tmp); unlink(tmp); return -1; }
    if (rename(tmp, path)) { snprintf(err, errlen, "rename to %s: %m", path); unlink(tmp); return -1; }
    return 0;
}

static int is_key(const char *line, const char *key) {
    size_t n = strlen(key);
    while (isspace((unsigned char)*line)) line++;
    return !strncmp(line, key, n) && (isspace((unsigned char)line[n]) || line[n] == '=');
}

// Replace (or append) the "key = value" lines of the given keys, keeping
// everything else (comments, other keys, order).
int conf_set_keys(const char *path, const char *const keys[], const char *const vals[], int n, char *err,
                  size_t errlen) {
    char *out = NULL;
    size_t outlen = 0;
    int done[8] = { 0 };
    if (n < 1 || n > 8) { snprintf(err, errlen, "conf_set_keys: %d keys", n); return -1; }
    FILE *mem = open_memstream(&out, &outlen);
    if (!mem) { snprintf(err, errlen, "out of memory"); return -1; }
    FILE *f = fopen(path, "r");
    if (f) {
        char line[PATH_MAX + 64];
        while (fgets(line, sizeof line, f)) {
            int k = 0;
            while (k < n && !is_key(line, keys[k])) k++;
            if (k == n) { fputs(line, mem); continue; }
            if (!done[k]) fprintf(mem, "%s = %s\n", keys[k], vals[k]); // the first line of a key is replaced,
            done[k] = 1;                                                 // repeats are dropped
        }
        fclose(f);
    } else if (errno != ENOENT) {
        fclose(mem);
        free(out);
        snprintf(err, errlen, "%s: %m", path);
        return -1;
    } else {
        fprintf(mem, "# qvue-warpd settings changed at runtime (qvue-warpctl on/off/toggle, mirror).\n"
                     "# Other keys from the default warp.conf may be added here.\n");
    }
    for (int k = 0; k < n; k++)
        if (!done[k]) fprintf(mem, "%s = %s\n", keys[k], vals[k]);
    fclose(mem);
    int rc = write_atomic(path, out, outlen, err, errlen);
    free(out);
    return rc;
}

int conf_set_enabled(const char *path, int enabled, char *err, size_t errlen) {
    const char *k[] = { "enabled" }, *v[] = { enabled ? "true" : "false" };
    return conf_set_keys(path, k, v, 1, err, errlen);
}

static int usable_dir(const char *dir) {
    struct stat st;
    return !stat(dir, &st) && S_ISDIR(st.st_mode) && !access(dir, W_OK);
}

// /run/qvue-warp (RuntimeDirectory= of the unit), else $XDG_RUNTIME_DIR/qvue-warp, else /tmp/qvue-warp
int pick_run_dir(char *out, size_t outlen) {
    if (usable_dir("/run/qvue-warp")) { snprintf(out, outlen, "/run/qvue-warp"); return 0; }
    const char *x = getenv("XDG_RUNTIME_DIR");
    if (x && *x && usable_dir(x)) snprintf(out, outlen, "%s/qvue-warp", x);
    else snprintf(out, outlen, "/tmp/qvue-warp");
    mkdir(out, 0775);
    return usable_dir(out) ? 0 : -1;
}
