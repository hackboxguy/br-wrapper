/*
 * kodi-drm-mirror.c - LD_PRELOAD shim that clones Kodi's GBM/DRM output
 * onto a second display connector (e.g. Pi4 HDMI-A-2).
 *
 * Kodi's GBM windowing drives exactly one connector/CRTC. This shim hooks
 * drmModeAtomicAddProperty() and, for every property Kodi sets on its own
 * planes / connector / CRTC, adds the equivalent property for a second
 * CRTC + its planes. Both CRTCs then scan out the very same framebuffers
 * (GUI plane and DRM-PRIME video plane) - zero copy, same as the kernel's
 * fbdev emulation does for the Qt linuxfb launcher.
 *
 * Environment:
 *   KODI_MIRROR_CONNECTOR  connector name to clone onto (default: the 2nd
 *                          connected connector, Kodi uses the 1st)
 *   KODI_MIRROR_MODE       WxH[@R] mode for the mirror (default: mode the
 *                          mirror CRTC currently runs, else preferred mode)
 *   KODI_MIRROR_DISABLE=1  pass-through, no mirroring
 *   KODI_MIRROR_DEBUG=1    verbose log
 *   KODI_MIRROR_LOG=file   log to file instead of stderr
 *
 * Build: gcc -O2 -shared -fPIC -o libkodi-drm-mirror.so kodi-drm-mirror.c -ldl
 * Use:   LD_PRELOAD=/path/libkodi-drm-mirror.so kodi --standalone
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- minimal libdrm ABI (xf86drmMode.h) - no -dev headers needed ---- */
typedef struct _drmModeAtomicReq *drmModeAtomicReqPtr;
typedef struct {
	uint32_t clock;
	uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew;
	uint16_t vdisplay, vsync_start, vsync_end, vtotal, vscan;
	uint32_t vrefresh, flags, type;
	char name[32];
} drmModeModeInfo;
typedef struct {
	int count_fbs; uint32_t *fbs;
	int count_crtcs; uint32_t *crtcs;
	int count_connectors; uint32_t *connectors;
	int count_encoders; uint32_t *encoders;
	uint32_t min_width, max_width, min_height, max_height;
} drmModeRes;
typedef struct {
	uint32_t connector_id, encoder_id, connector_type, connector_type_id;
	int connection;
	uint32_t mmWidth, mmHeight;
	int subpixel;
	int count_modes; drmModeModeInfo *modes;
	int count_props; uint32_t *props; uint64_t *prop_values;
	int count_encoders; uint32_t *encoders;
} drmModeConnector;
typedef struct {
	uint32_t encoder_id, encoder_type, crtc_id, possible_crtcs, possible_clones;
} drmModeEncoder;
typedef struct {
	uint32_t crtc_id, buffer_id, x, y, width, height;
	int mode_valid;
	drmModeModeInfo mode;
	int gamma_size;
} drmModeCrtc;
typedef struct { uint32_t count_planes; uint32_t *planes; } drmModePlaneRes;
typedef struct {
	uint32_t count_formats; uint32_t *formats;
	uint32_t plane_id, crtc_id, fb_id, crtc_x, crtc_y, x, y;
	uint32_t possible_crtcs, gamma_size;
} drmModePlane;
typedef struct { uint32_t count_props; uint32_t *props; uint64_t *prop_values; } drmModeObjectProperties;
typedef struct { uint32_t prop_id, flags; char name[32]; /* rest unused */ } drmModePropertyRes;
typedef struct { uint32_t id, length; void *data; } drmModePropertyBlobRes;

#define OBJ_CRTC      0xccccccccu
#define OBJ_CONNECTOR 0xc0c0c0c0u
#define OBJ_PLANE     0xeeeeeeeeu
#define MODE_TYPE_PREFERRED (1 << 3)
#define DRM_CLIENT_CAP_UNIVERSAL_PLANES 2
#define DRM_CLIENT_CAP_ATOMIC           3

/* ---- real libdrm entry points ---- */
#define REAL(ret, name, args) static ret (*real_##name) args
REAL(int, drmModeAtomicAddProperty, (drmModeAtomicReqPtr, uint32_t, uint32_t, uint64_t));
REAL(drmModeRes *, drmModeGetResources, (int));
REAL(void, drmModeFreeResources, (drmModeRes *));
REAL(drmModeConnector *, drmModeGetConnector, (int, uint32_t));
REAL(void, drmModeFreeConnector, (drmModeConnector *));
REAL(drmModeEncoder *, drmModeGetEncoder, (int, uint32_t));
REAL(void, drmModeFreeEncoder, (drmModeEncoder *));
REAL(drmModeCrtc *, drmModeGetCrtc, (int, uint32_t));
REAL(void, drmModeFreeCrtc, (drmModeCrtc *));
REAL(drmModePlaneRes *, drmModeGetPlaneResources, (int));
REAL(void, drmModeFreePlaneResources, (drmModePlaneRes *));
REAL(drmModePlane *, drmModeGetPlane, (int, uint32_t));
REAL(void, drmModeFreePlane, (drmModePlane *));
REAL(drmModeObjectProperties *, drmModeObjectGetProperties, (int, uint32_t, uint32_t));
REAL(void, drmModeFreeObjectProperties, (drmModeObjectProperties *));
REAL(drmModePropertyRes *, drmModeGetProperty, (int, uint32_t));
REAL(void, drmModeFreeProperty, (drmModePropertyRes *));
REAL(drmModePropertyBlobRes *, drmModeGetPropertyBlob, (int, uint32_t));
REAL(void, drmModeFreePropertyBlob, (drmModePropertyBlobRes *));
REAL(int, drmModeCreatePropertyBlob, (int, const void *, size_t, uint32_t *));
REAL(int, drmSetClientCap, (int, uint64_t, uint64_t));

static void resolve(void)
{
#define R(name) real_##name = dlsym(RTLD_NEXT, #name)
	R(drmModeAtomicAddProperty); R(drmModeGetResources); R(drmModeFreeResources);
	R(drmModeGetConnector); R(drmModeFreeConnector); R(drmModeGetEncoder);
	R(drmModeFreeEncoder); R(drmModeGetCrtc); R(drmModeFreeCrtc);
	R(drmModeGetPlaneResources); R(drmModeFreePlaneResources); R(drmModeGetPlane);
	R(drmModeFreePlane); R(drmModeObjectGetProperties); R(drmModeFreeObjectProperties);
	R(drmModeGetProperty); R(drmModeFreeProperty); R(drmModeGetPropertyBlob);
	R(drmModeFreePropertyBlob); R(drmModeCreatePropertyBlob); R(drmSetClientCap);
#undef R
}

/* ---- object/property cache ---- */
#define MAX_OBJS  96
#define MAX_PROPS 48
struct obj {
	uint32_t id, type;
	int nprops;
	uint32_t prop_id[MAX_PROPS];
	char prop_name[MAX_PROPS][32];
	/* planes */
	int plane_type;      /* 0 overlay, 1 primary, 2 cursor */
	uint32_t possible_crtcs;
	uint32_t mirror;     /* plane: mirror plane id */
};

static struct obj objs[MAX_OBJS];
static int nobjs;
static int g_state;  /* 0 = uninit, 1 = active, -1 = disabled */
static int g_fd = -1, g_debug;
static uint32_t m_conn, m_crtc, m_mode_blob;
static drmModeModeInfo m_mode;
static uint16_t src_w, src_h;  /* Kodi's mode (for scaling) */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static FILE *g_log;
#define LOG(...)  do { fprintf(g_log ? g_log : stderr, "kodi-drm-mirror: " __VA_ARGS__); \
			if (g_log) fflush(g_log); } while (0)
#define DBG(...)  do { if (g_debug) LOG(__VA_ARGS__); } while (0)

static struct obj *find_obj(uint32_t id)
{
	for (int i = 0; i < nobjs; i++)
		if (objs[i].id == id)
			return &objs[i];
	return NULL;
}

static const char *prop_name(struct obj *o, uint32_t prop)
{
	for (int i = 0; i < o->nprops; i++)
		if (o->prop_id[i] == prop)
			return o->prop_name[i];
	return NULL;
}

static uint32_t prop_id(struct obj *o, const char *name)
{
	for (int i = 0; i < o->nprops; i++)
		if (!strcmp(o->prop_name[i], name))
			return o->prop_id[i];
	return 0;
}

static struct obj *add_obj(int fd, uint32_t id, uint32_t type)
{
	if (nobjs >= MAX_OBJS)
		return NULL;
	struct obj *o = &objs[nobjs++];
	memset(o, 0, sizeof(*o));
	o->id = id;
	o->type = type;
	drmModeObjectProperties *p = real_drmModeObjectGetProperties(fd, id, type);
	if (!p)
		return o;
	for (uint32_t i = 0; i < p->count_props && o->nprops < MAX_PROPS; i++) {
		drmModePropertyRes *pr = real_drmModeGetProperty(fd, p->props[i]);
		if (!pr)
			continue;
		o->prop_id[o->nprops] = pr->prop_id;
		snprintf(o->prop_name[o->nprops], 32, "%s", pr->name);
		if (type == OBJ_PLANE && !strcmp(pr->name, "type"))
			o->plane_type = (int)p->prop_values[i];
		o->nprops++;
		real_drmModeFreeProperty(pr);
	}
	real_drmModeFreeObjectProperties(p);
	return o;
}

static const char *conn_type_name(uint32_t t)
{
	static const char *n[] = { "Unknown", "VGA", "DVI-I", "DVI-D", "DVI-A",
		"Composite", "SVIDEO", "LVDS", "Component", "DIN", "DP", "HDMI-A",
		"HDMI-B", "TV", "eDP", "Virtual", "DSI", "DPI", "Writeback", "SPI", "USB" };
	return t < sizeof(n) / sizeof(n[0]) ? n[t] : "Unknown";
}

static int pick_mode(drmModeConnector *c, drmModeCrtc *cur)
{
	const char *want = getenv("KODI_MIRROR_MODE");
	if (want && *want) {
		unsigned w = 0, h = 0, r = 0;
		sscanf(want, "%ux%u@%u", &w, &h, &r);
		for (int i = 0; i < c->count_modes; i++)
			if (c->modes[i].hdisplay == w && c->modes[i].vdisplay == h &&
			    (!r || c->modes[i].vrefresh == r)) {
				m_mode = c->modes[i];
				return 0;
			}
		LOG("mode %s not found on mirror connector, using default\n", want);
	}
	if (cur && cur->mode_valid) {
		m_mode = cur->mode;
		return 0;
	}
	for (int i = 0; i < c->count_modes; i++)
		if (c->modes[i].type & MODE_TYPE_PREFERRED) {
			m_mode = c->modes[i];
			return 0;
		}
	if (c->count_modes > 0) {
		m_mode = c->modes[0];
		return 0;
	}
	return -1;
}

/* Returns 0 if mirroring was set up on this fd. */
static int setup(int fd)
{
	/*
	 * Kodi enables these right after drmModeGetResources(); without them the
	 * atomic properties (FB_ID, CRTC_ID, MODE_ID...) and primary/cursor
	 * planes are hidden from us.
	 */
	if (real_drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) ||
	    real_drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1))
		return -1;

	drmModeRes *res = real_drmModeGetResources(fd);
	if (!res)
		return -1;
	if (res->count_connectors < 2 || res->count_crtcs < 2) {
		real_drmModeFreeResources(res);
		return -1;
	}

	const char *want = getenv("KODI_MIRROR_CONNECTOR");
	drmModeConnector *mc = NULL;
	int nconnected = 0;
	for (int i = 0; i < res->count_connectors && !mc; i++) {
		drmModeConnector *c = real_drmModeGetConnector(fd, res->connectors[i]);
		if (!c)
			continue;
		char name[48];
		snprintf(name, sizeof(name), "%s-%u", conn_type_name(c->connector_type),
			 c->connector_type_id);
		int ok = 0;
		if (c->connection == 1) {
			nconnected++;
			ok = want && *want ? !strcmp(name, want) : nconnected == 2;
		}
		if (ok) {
			mc = c;
			LOG("mirror connector: %s (id %u)\n", name, c->connector_id);
		} else {
			real_drmModeFreeConnector(c);
		}
	}
	if (!mc) {
		LOG("no mirror connector found (%s), mirroring disabled\n",
		    want && *want ? want : "need 2 connected displays");
		real_drmModeFreeResources(res);
		return -1;
	}
	m_conn = mc->connector_id;

	/* mirror CRTC: the one currently driving the connector, else a free one */
	drmModeEncoder *enc = mc->encoder_id ? real_drmModeGetEncoder(fd, mc->encoder_id) : NULL;
	if (enc && enc->crtc_id)
		m_crtc = enc->crtc_id;
	if (!m_crtc) {
		for (int e = 0; e < mc->count_encoders && !m_crtc; e++) {
			drmModeEncoder *en = real_drmModeGetEncoder(fd, mc->encoders[e]);
			if (!en)
				continue;
			/* last possible CRTC: Kodi takes the first one */
			for (int i = res->count_crtcs - 1; i >= 0 && !m_crtc; i--)
				if (en->possible_crtcs & (1u << i))
					m_crtc = res->crtcs[i];
			real_drmModeFreeEncoder(en);
		}
	}
	if (enc)
		real_drmModeFreeEncoder(enc);

	int m_crtc_idx = -1;
	for (int i = 0; i < res->count_crtcs; i++)
		if (res->crtcs[i] == m_crtc)
			m_crtc_idx = i;
	drmModeCrtc *cur = m_crtc ? real_drmModeGetCrtc(fd, m_crtc) : NULL;
	int mode_ok = m_crtc_idx >= 0 ? pick_mode(mc, cur) : -1;
	if (cur)
		real_drmModeFreeCrtc(cur);
	real_drmModeFreeConnector(mc);
	if (mode_ok < 0) {
		LOG("no CRTC/mode for mirror connector, mirroring disabled\n");
		real_drmModeFreeResources(res);
		return -1;
	}
	if (real_drmModeCreatePropertyBlob(fd, &m_mode, sizeof(m_mode), &m_mode_blob)) {
		LOG("failed to create mode blob, mirroring disabled\n");
		real_drmModeFreeResources(res);
		return -1;
	}
	LOG("mirror crtc %u mode %ux%u@%u\n", m_crtc, m_mode.hdisplay, m_mode.vdisplay,
	    m_mode.vrefresh);

	/* cache crtcs + connectors */
	for (int i = 0; i < res->count_crtcs; i++)
		add_obj(fd, res->crtcs[i], OBJ_CRTC);
	for (int i = 0; i < res->count_connectors; i++)
		add_obj(fd, res->connectors[i], OBJ_CONNECTOR);
	real_drmModeFreeResources(res);

	/* cache planes; split into "mirror-crtc" planes and the rest */
	drmModePlaneRes *pres = real_drmModeGetPlaneResources(fd);
	if (!pres)
		return -1;
	uint32_t mbit = 1u << m_crtc_idx;
	for (uint32_t i = 0; i < pres->count_planes; i++) {
		drmModePlane *p = real_drmModeGetPlane(fd, pres->planes[i]);
		if (!p)
			continue;
		struct obj *o = add_obj(fd, p->plane_id, OBJ_PLANE);
		if (o)
			o->possible_crtcs = p->possible_crtcs;
		real_drmModeFreePlane(p);
	}
	real_drmModeFreePlaneResources(pres);

	/*
	 * Pair every plane that cannot be used on the mirror CRTC with a plane of
	 * the same type that is exclusive to the mirror CRTC (vc4 creates planes
	 * per CRTC symmetrically, so pairing in enumeration order works).
	 */
	for (int t = 0; t <= 2; t++) {
		int j = 0;
		for (int i = 0; i < nobjs; i++) {
			struct obj *s = &objs[i];
			if (s->type != OBJ_PLANE || s->plane_type != t || (s->possible_crtcs & mbit))
				continue;
			for (; j < nobjs; j++) {
				struct obj *d = &objs[j];
				if (d->type == OBJ_PLANE && d->plane_type == t &&
				    d->possible_crtcs == mbit) {
					s->mirror = d->id;
					DBG("plane %u (type %d) -> mirror plane %u\n", s->id, t, d->id);
					j++;
					break;
				}
			}
		}
	}
	return 0;
}

static void init_once(int fd)
{
	if (g_state)
		return;
	pthread_mutex_lock(&g_lock);
	if (!g_state) {
		const char *lf = getenv("KODI_MIRROR_LOG");
		if (lf && *lf)
			g_log = fopen(lf, "a");
		g_debug = getenv("KODI_MIRROR_DEBUG") && atoi(getenv("KODI_MIRROR_DEBUG"));
		const char *dis = getenv("KODI_MIRROR_DISABLE");
		if (dis && atoi(dis)) {
			g_state = -1;
		} else if (setup(fd) == 0) {
			g_fd = fd;
			g_state = 1;
		} else {
			/*
			 * Single display (or no usable mirror): decide once and stay
			 * pass-through for this Kodi run, so a display hot-plugged
			 * later can't start mirroring without a full modeset.
			 */
			nobjs = 0;
			g_state = -1;
		}
	}
	pthread_mutex_unlock(&g_lock);
}

/* ---- hooks ---- */

/* Kodi enumerates resources on each DRM device it opens: grab the KMS fd. */
drmModeRes *drmModeGetResources(int fd)
{
	if (!real_drmModeGetResources)
		resolve();
	drmModeRes *r = real_drmModeGetResources(fd);
	if (r && r->count_connectors > 1 && !g_state)
		init_once(fd);
	return r;
}

static uint64_t scale(uint64_t v, uint16_t to, uint16_t from)
{
	if (!from || to == from)
		return v;
	return (uint64_t)(((int64_t)(int32_t)v * to) / from) & 0xffffffffu;
}

int drmModeAtomicAddProperty(drmModeAtomicReqPtr req, uint32_t object_id,
			     uint32_t property_id, uint64_t value)
{
	if (!real_drmModeAtomicAddProperty)
		resolve();
	int ret = real_drmModeAtomicAddProperty(req, object_id, property_id, value);
	if (ret < 0 || g_state != 1)
		return ret;

	struct obj *o = find_obj(object_id);
	if (!o)
		return ret;
	const char *name = prop_name(o, property_id);
	if (!name)
		return ret;

	uint32_t dst = 0, dprop = 0;
	uint64_t v = value;

	if (o->type == OBJ_PLANE && o->mirror) {
		struct obj *d = find_obj(o->mirror);
		if (!d || !(dprop = prop_id(d, name)))
			return ret;
		dst = d->id;
		if (!strcmp(name, "CRTC_ID"))
			v = value ? m_crtc : 0;
		else if (!strcmp(name, "CRTC_X") || !strcmp(name, "CRTC_W"))
			v = scale(value, m_mode.hdisplay, src_w);
		else if (!strcmp(name, "CRTC_Y") || !strcmp(name, "CRTC_H"))
			v = scale(value, m_mode.vdisplay, src_h);
	} else if (o->type == OBJ_CONNECTOR && o->id != m_conn) {
		if (strcmp(name, "CRTC_ID"))
			return ret;  /* colorspace / HDR metadata etc.: primary only */
		struct obj *d = find_obj(m_conn);
		if (!d || !(dprop = prop_id(d, name)))
			return ret;
		dst = m_conn;
		v = value ? m_crtc : 0;
	} else if (o->type == OBJ_CRTC && o->id != m_crtc) {
		struct obj *d = find_obj(m_crtc);
		if (!d)
			return ret;
		if (!strcmp(name, "MODE_ID")) {
			if (value) {
				drmModePropertyBlobRes *b = real_drmModeGetPropertyBlob(g_fd, (uint32_t)value);
				if (b && b->length >= sizeof(drmModeModeInfo)) {
					drmModeModeInfo *mi = b->data;
					src_w = mi->hdisplay;
					src_h = mi->vdisplay;
					DBG("kodi mode %ux%u@%u\n", src_w, src_h, mi->vrefresh);
				}
				if (b)
					real_drmModeFreePropertyBlob(b);
			}
			v = value ? m_mode_blob : 0;
		} else if (strcmp(name, "ACTIVE")) {
			return ret;  /* gamma/CTM/OUT_FENCE_PTR: primary only */
		}
		dst = m_crtc;
		dprop = prop_id(d, name);
	} else {
		return ret;
	}

	if (dst && dprop) {
		DBG("mirror %u.%s=%llu -> %u=%llu\n", object_id, name,
		    (unsigned long long)value, dst, (unsigned long long)v);
		int r2 = real_drmModeAtomicAddProperty(req, dst, dprop, v);
		if (r2 >= 0)
			ret = r2;
	}
	return ret;
}
