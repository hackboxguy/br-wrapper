#define _GNU_SOURCE   /* accept4 */
/* control.c - the control socket (--control=PATH, default CONTROL_DEFAULT)
 *
 * A Unix stream socket other programs (the Stream Deck's Player keys, scripts)
 * use to drive the playback. One command per connection, one line each way:
 *
 *   pause | resume | toggle   freeze on the frame on screen / go on from it
 *   seek +S | seek -S         jump S seconds (fractions allowed) forward / back;
 *                             requests that arrive before the presenter takes
 *                             them add up. Playlist mode: within the video on
 *                             screen (back before its start = its start; past
 *                             its end = the next item); on an image + = next
 *                             item, - = previous item
 *   seek S                    jump to S seconds into the video on screen
 *   stop                      stop playback, like the EXIT button (exit code 0)
 *   status                    one line of JSON: player, mode, state, item, items,
 *                             file, position, duration (seconds)
 *
 * The reply is "ok ..." or "error <reason>" (status: the JSON). Pause and
 * seek are not offered with --sync: the two players would have to agree on
 * the moment over the network (not done yet).
 *
 * The main loop owns the socket; the presenter thread takes the requests at
 * its own pace (control_paused(), control_take_seek()) and publishes what it
 * shows (control_publish()); player_wake() gets it out of a still.
 */
#include "player.h"
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#define LINE_MAX_LEN 256

struct client {
	struct player *p;
	int fd;
	GString *in;
};

static void send_line(int fd, const char *s)
{
	size_t len = strlen(s), off = 0;
	while (off < len) {
		ssize_t n = write(fd, s + off, len - off);
		if (n <= 0) {
			if (n < 0 && errno == EINTR)
				continue;
			return;
		}
		off += (size_t)n;
	}
	if (write(fd, "\n", 1) < 0)
		return;
}

static void status_json(struct player *p, GString *out)
{
	g_mutex_lock(&p->ctl_lock);
	gchar *file = g_strescape(p->st_file, NULL);
	const char *mode = p->sync ? "sync" : p->pl ? "playlist" : "two-file";
	g_string_append_printf(out,
			       "{\"player\":\"dual-video-player\",\"mode\":\"%s\",\"state\":\"%s\","
			       "\"item\":%d,\"items\":%d,\"file\":\"%s\",\"position\":%.3f,\"duration\":%.3f}",
			       mode, p->st_state, p->st_item + 1, p->st_items, file,
			       p->st_pos_ns / 1e9, p->st_dur_ns / 1e9);
	g_free(file);
	g_mutex_unlock(&p->ctl_lock);
}

static void handle(struct player *p, gchar *line, GString *out)
{
	g_strstrip(line);
	gchar **w = g_strsplit_set(line, " \t", 3);
	const char *cmd = w[0] ? w[0] : "";
	const char *arg = w[0] && w[1] ? w[1] : NULL;
	gboolean timed = !strcmp(cmd, "pause") || !strcmp(cmd, "resume") ||
			 !strcmp(cmd, "toggle") || !strcmp(cmd, "seek");

	if (timed && p->sync) {
		g_string_append(out, "error not supported with --sync");
	} else if (!strcmp(cmd, "status")) {
		status_json(p, out);
	} else if (!strcmp(cmd, "stop")) {
		g_print("dual-video-player: control: stop\n");
		g_string_append(out, "ok stopping");
		g_main_loop_quit(p->loop);
	} else if (!strcmp(cmd, "pause") || !strcmp(cmd, "resume") || !strcmp(cmd, "toggle")) {
		int want = !strcmp(cmd, "pause") ? 1 : !strcmp(cmd, "resume") ? 0 :
			   !g_atomic_int_get(&p->want_pause);
		g_atomic_int_set(&p->want_pause, want);
		player_wake(p);
		g_string_append_printf(out, "ok %s", want ? "paused" : "playing");
	} else if (!strcmp(cmd, "seek") && arg) {
		char *end = NULL;
		double s = g_ascii_strtod(arg, &end);
		if (end == arg || *end || !isfinite(s) || fabs(s) > 24 * 3600) {
			g_string_append(out, "error seek needs seconds, e.g. seek -10");
		} else {
			gboolean rel = arg[0] == '+' || arg[0] == '-';
			g_mutex_lock(&p->ctl_lock);
			if (rel) {
				if (p->seek_abs_ns >= 0)
					p->seek_abs_ns = MAX(0, p->seek_abs_ns + (gint64)llround(s * 1e9));
				else
					p->seek_rel_ns += (gint64)llround(s * 1e9);
				p->seek_pending = TRUE;
			} else if (s >= 0) {
				p->seek_abs_ns = (gint64)llround(s * 1e9);
				p->seek_rel_ns = 0;
				p->seek_pending = TRUE;
			}
			g_mutex_unlock(&p->ctl_lock);
			if (!rel && s < 0)
				g_string_append(out, "error seek position below 0");
			else {
				player_wake(p);
				g_string_append_printf(out, "ok seek %s", arg);
			}
		}
	} else {
		g_string_append(out, "error unknown command (pause, resume, toggle, seek S, stop, status)");
	}
	g_strfreev(w);
}

static gboolean on_client(gint fd, GIOCondition cond, gpointer data)
{
	struct client *c = data;
	char buf[128];
	gboolean done = FALSE;
	if (cond & G_IO_IN) {
		ssize_t n = read(fd, buf, sizeof(buf));
		if (n > 0)
			g_string_append_len(c->in, buf, n);
		else if (n == 0 || (errno != EAGAIN && errno != EINTR))
			done = TRUE;   /* EOF: take what came as the line */
	} else {
		done = TRUE;
	}
	char *nl = memchr(c->in->str, '\n', c->in->len);
	if (nl || done || c->in->len > LINE_MAX_LEN) {
		if (nl)
			*nl = 0;
		GString *out = g_string_new(NULL);
		if (c->in->len > LINE_MAX_LEN && !nl)
			g_string_append(out, "error line too long");
		else if (c->in->len || nl)
			handle(c->p, c->in->str, out);
		if (out->len)
			send_line(fd, out->str);
		g_string_free(out, TRUE);
		g_string_free(c->in, TRUE);
		close(fd);
		g_free(c);
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

static gboolean on_accept(gint fd, GIOCondition cond, gpointer data)
{
	struct player *p = data;
	(void)cond;
	int cfd = accept4(fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
	if (cfd < 0)
		return G_SOURCE_CONTINUE;
	struct client *c = g_new0(struct client, 1);
	c->p = p;
	c->fd = cfd;
	c->in = g_string_new(NULL);
	g_unix_fd_add(cfd, G_IO_IN | G_IO_HUP | G_IO_ERR, on_client, c);
	return G_SOURCE_CONTINUE;
}

void control_init(struct player *p)
{
	g_mutex_init(&p->ctl_lock);
	p->ctl_fd = -1;
	p->seek_abs_ns = -1;
	p->st_item = -1;
	g_strlcpy(p->st_state, "starting", sizeof(p->st_state));
}

/* listen on @path (NULL or "none": no socket); call with p->loop set */
void control_start(struct player *p, const char *path)
{
	if (!path || !strcmp(path, "none"))
		return;
	struct sockaddr_un a = { .sun_family = AF_UNIX };
	if (strlen(path) >= sizeof(a.sun_path)) {
		g_printerr("dual-video-player: --control: path too long\n");
		return;
	}
	g_strlcpy(a.sun_path, path, sizeof(a.sun_path));
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return;
	/* a previous player that died left its socket file: only one player can
	 * drive the displays, so whatever is there is stale */
	unlink(path);
	mode_t old = umask(0117);   /* owner and group read/write */
	int r = bind(fd, (struct sockaddr *)&a, sizeof(a));
	umask(old);
	if (r || listen(fd, 4)) {
		g_printerr("dual-video-player: control socket %s: %s\n", path, strerror(errno));
		close(fd);
		return;
	}
	p->ctl_fd = fd;
	p->ctl_path = path;
	p->ctl_src = g_unix_fd_add(fd, G_IO_IN, on_accept, p);
	g_print("dual-video-player: control socket %s\n", path);
}

void control_stop(struct player *p)
{
	if (p->ctl_fd < 0)
		return;
	g_source_remove(p->ctl_src);
	close(p->ctl_fd);
	p->ctl_fd = -1;
	unlink(p->ctl_path);
}

gboolean control_paused(struct player *p)
{
	return g_atomic_int_get(&p->want_pause) != 0;
}

/* a pending seek, as a target position for a video now at @pos (ns, may be < 0) */
gboolean control_take_seek(struct player *p, gint64 pos, gint64 *target)
{
	gboolean have;
	g_mutex_lock(&p->ctl_lock);
	have = p->seek_pending;
	if (have) {
		*target = p->seek_abs_ns >= 0 ? p->seek_abs_ns : pos + p->seek_rel_ns;
		p->seek_pending = FALSE;
		p->seek_abs_ns = -1;
		p->seek_rel_ns = 0;
	}
	g_mutex_unlock(&p->ctl_lock);
	return have;
}

/* drop a pending seek (e.g. nothing seekable on screen) */
gint64 control_drop_seek(struct player *p)
{
	gint64 rel = 0;
	g_mutex_lock(&p->ctl_lock);
	if (p->seek_pending)
		rel = p->seek_abs_ns >= 0 ? 0 : p->seek_rel_ns;
	p->seek_pending = FALSE;
	p->seek_abs_ns = -1;
	p->seek_rel_ns = 0;
	g_mutex_unlock(&p->ctl_lock);
	return rel;
}

gboolean control_seek_pending(struct player *p)
{
	g_mutex_lock(&p->ctl_lock);
	gboolean have = p->seek_pending;
	g_mutex_unlock(&p->ctl_lock);
	return have;
}

void control_publish(struct player *p, const char *state, int item, int items, const char *file,
		     gint64 pos, gint64 dur)
{
	g_mutex_lock(&p->ctl_lock);
	g_strlcpy(p->st_state, state, sizeof(p->st_state));
	p->st_item = item;
	p->st_items = items;
	if (file) {
		const char *base = strrchr(file, '/');
		g_strlcpy(p->st_file, base ? base + 1 : file, sizeof(p->st_file));
	}
	p->st_pos_ns = MAX(0, pos);
	p->st_dur_ns = MAX(0, dur);
	g_mutex_unlock(&p->ctl_lock);
}
