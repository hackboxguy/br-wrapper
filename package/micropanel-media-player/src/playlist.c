/* playlist.c - micropanel-playlist.json (dual-video-player --playlist)
 *
 *   { "version": 1, "image_duration_s": 10, "loop": true, "autostart": false,
 *     "items": ["Videos/intro.mp4", "Pictures/slide-01.jpg"] }
 *
 * Item paths are relative to the directory holding the playlist (the stick's
 * root) or to @root when given (a playlist kept off a read-only stick), "/"
 * separated. Unknown keys are ignored; missing ones take the
 * defaults below.
 */
#include "player.h"
#include <json-glib/json-glib.h>

#define DEFAULT_IMAGE_DURATION_S 10

/* "a/../b" climbs out of the stick, "holiday..2.mp4" is just a name */
static gboolean has_parent_component(const char *rel)
{
	gchar **parts = g_strsplit(rel, "/", -1);
	gboolean found = FALSE;
	for (gchar **c = parts; *c && !found; c++)
		found = !strcmp(*c, "..");
	g_strfreev(parts);
	return found;
}

struct playlist *playlist_load(const char *file, const char *root, GError **err)
{
	JsonParser *parser = json_parser_new();
	if (!json_parser_load_from_file(parser, file, err)) {
		g_object_unref(parser);
		return NULL;
	}
	JsonNode *doc = json_parser_get_root(parser);
	if (!doc || !JSON_NODE_HOLDS_OBJECT(doc)) {
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_INVAL, "%s: not a JSON object", file);
		g_object_unref(parser);
		return NULL;
	}
	JsonObject *o = json_node_get_object(doc);
	struct playlist *pl = g_new0(struct playlist, 1);
	pl->file = g_strdup(file);
	pl->root = root ? g_strdup(root) : g_path_get_dirname(file);
	pl->image_duration_s = (int)json_object_get_int_member_with_default(o, "image_duration_s",
									     DEFAULT_IMAGE_DURATION_S);
	if (pl->image_duration_s < 1)
		pl->image_duration_s = DEFAULT_IMAGE_DURATION_S;
	pl->loop = json_object_get_boolean_member_with_default(o, "loop", FALSE);
	pl->autostart = json_object_get_boolean_member_with_default(o, "autostart", FALSE);
	pl->items = g_ptr_array_new_with_free_func(g_free);
	JsonArray *items = json_object_has_member(o, "items") ?
			   json_object_get_array_member(o, "items") : NULL;
	for (guint i = 0; items && i < json_array_get_length(items); i++) {
		JsonNode *n = json_array_get_element(items, i);
		const char *rel = JSON_NODE_HOLDS_VALUE(n) ? json_node_get_string(n) : NULL;
		if (!rel || !*rel || rel[0] == '/' || has_parent_component(rel))
			continue;   /* relative paths inside the stick only */
		g_ptr_array_add(pl->items, g_build_filename(pl->root, rel, NULL));
	}
	g_object_unref(parser);
	if (!pl->items->len) {
		g_set_error(err, G_FILE_ERROR, G_FILE_ERROR_INVAL, "%s: no items", file);
		playlist_free(pl);
		return NULL;
	}
	return pl;
}

void playlist_free(struct playlist *pl)
{
	if (!pl)
		return;
	g_free(pl->file);
	g_free(pl->root);
	if (pl->items)
		g_ptr_array_unref(pl->items);
	g_free(pl);
}

/*
 * --autostart-check: may this playlist start by itself at boot? The flag is
 * read first (no probing when it is off), then items are classified only
 * until the first playable one. One line of reason; 0 = start.
 */
int playlist_autostart_check(struct playlist *pl)
{
	if (!pl->autostart) {
		g_print("the playlist does not ask for autostart\n");
		return 1;
	}
	for (guint i = 0; i < pl->items->len; i++) {
		struct media_info mi;
		if (probe_file(g_ptr_array_index(pl->items, i), &mi)) {
			g_print("autostart: item %u of %u plays\n", i + 1, pl->items->len);
			return 0;
		}
	}
	g_print("nothing in the playlist can be played\n");
	return 1;
}

/* --list: the resolved plan, one probe line per item, no display */
int playlist_list(struct playlist *pl)
{
	int playable = 0;
	g_print("PLAYLIST\titems=%u\timage_duration_s=%d\tloop=%d\tautostart=%d\tfile=%s\n",
		pl->items->len, pl->image_duration_s, pl->loop, pl->autostart, pl->file);
	for (guint i = 0; i < pl->items->len; i++) {
		struct media_info mi;
		if (probe_file(g_ptr_array_index(pl->items, i), &mi))
			playable++;
		probe_print(&mi);
	}
	return playable ? 0 : 1;
}
