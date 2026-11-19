// GL program and mesh draw into the stacked 1500x894 buffer.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qvuewarp.h"

#include <GLES2/gl2ext.h>   // GL_TEXTURE_EXTERNAL_OES

#define SW QW_PW
#define SH (QW_PH * QW_NPANEL)

static const char *VS =
    "#version 300 es\n"
    "layout(location=0) in vec2 aPos;\n"
    "layout(location=1) in vec2 aSrc;\n"
    "uniform vec2 uOrigin;\n"   // source pixel of texel (0,0)
    "uniform vec2 uInv;\n"      // 1 / texture size
    "out vec2 vUV;\n"
    "void main() { vUV = (aSrc - uOrigin + 0.5) * uInv; gl_Position = vec4(aPos, 0.0, 1.0); }\n";

static const char *FS =
    "#version 300 es\n"
    "precision highp float;\n"
    "in vec2 vUV;\n"
    "uniform sampler2D uTex;\n"
    "out vec4 oColor;\n"
    "void main() { oColor = vec4(texture(uTex, vUV).rgb, 1.0); }\n";

// video and images: frame bounds, chroma planes, YUV matrix
static const char *VS2 =
    "#version 300 es\n"
    "layout(location=0) in vec2 aPos;\n"
    "layout(location=1) in vec2 aSrc;\n"
    "uniform vec2 uOrigin;\n"
    "uniform vec2 uInv;\n"
    "out vec2 vUV;\n"
    "out vec2 vSrc;\n"         // source pixel position (edges at integers)
    "void main() { vUV = (aSrc - uOrigin + 0.5) * uInv; vSrc = aSrc + 0.5; gl_Position = vec4(aPos, 0.0, 1.0); }\n";

#define FS2_HEAD \
    "precision highp float;\n" \
    "in vec2 vUV;\n" \
    "in vec2 vSrc;\n" \
    "uniform vec2 uFrame;\n" \
    "uniform mat3 uM;\n" \
    "uniform vec3 uOff;\n" \
    "out vec4 oColor;\n" \
    "bool outside() { return any(lessThan(vSrc, vec2(0.0))) || any(greaterThan(vSrc, uFrame)); }\n" \
    "vec4 yuv(float y, float u, float v) { return vec4(clamp(uM * (vec3(y, u, v) - uOff), 0.0, 1.0), 1.0); }\n"

static const char *FS_RGBF =
    "#version 300 es\n" FS2_HEAD
    "uniform sampler2D uTex;\n"
    "void main() { oColor = outside() ? vec4(0.0, 0.0, 0.0, 1.0) : vec4(texture(uTex, vUV).rgb, 1.0); }\n";

static const char *FS_OES =
    "#version 300 es\n"
    "#extension GL_OES_EGL_image_external_essl3 : require\n" FS2_HEAD
    "uniform samplerExternalOES uTex;\n"
    "void main() { oColor = outside() ? vec4(0.0, 0.0, 0.0, 1.0) : vec4(texture(uTex, vUV).rgb, 1.0); }\n";

static const char *FS_NV12 =
    "#version 300 es\n" FS2_HEAD
    "uniform sampler2D uTex, uTex1;\n"
    "void main() { if (outside()) { oColor = vec4(0.0, 0.0, 0.0, 1.0); return; }\n"
    "  vec2 c = texture(uTex1, vSrc / uFrame).rg;\n"
    "  oColor = yuv(texture(uTex, vUV).r, c.r, c.g); }\n";

static const char *FS_I420 =
    "#version 300 es\n" FS2_HEAD
    "uniform sampler2D uTex, uTex1, uTex2;\n"
    "void main() { if (outside()) { oColor = vec4(0.0, 0.0, 0.0, 1.0); return; }\n"
    "  vec2 c = vSrc / uFrame;\n"
    "  oColor = yuv(texture(uTex, vUV).r, texture(uTex1, c).r, texture(uTex2, c).r); }\n";

static const char *OVS =
    "#version 300 es\n"
    "layout(location=0) in vec2 aPos;\n"
    "layout(location=1) in vec3 aCol;\n"
    "out vec3 vCol;\n"
    "void main() { vCol = aCol; gl_Position = vec4(aPos, 0.0, 1.0); }\n";

static const char *OFS =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec3 vCol;\n"
    "out vec4 oColor;\n"
    "void main() { oColor = vec4(vCol, 1.0); }\n";

static GLuint compile(GLenum t, const char *src, char *err, size_t errlen) {
    GLuint s = glCreateShader(t);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        snprintf(err, errlen, "shader: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint program(const char *vsrc, const char *fsrc, char *err, size_t errlen) {
    GLuint vs = compile(GL_VERTEX_SHADER, vsrc, err, errlen);
    if (!vs) return 0;
    GLuint fs = compile(GL_FRAGMENT_SHADER, fsrc, err, errlen);
    if (!fs) { glDeleteShader(vs); return 0; }
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { snprintf(err, errlen, "program link failed"); glDeleteProgram(p); return 0; }
    return p;
}

static void locate(struct qw_src_prog *sp) {
    sp->u_tex = glGetUniformLocation(sp->prog, "uTex");
    sp->u_tex1 = glGetUniformLocation(sp->prog, "uTex1");
    sp->u_tex2 = glGetUniformLocation(sp->prog, "uTex2");
    sp->u_origin = glGetUniformLocation(sp->prog, "uOrigin");
    sp->u_inv = glGetUniformLocation(sp->prog, "uInv");
    sp->u_frame = glGetUniformLocation(sp->prog, "uFrame");
    sp->u_m = glGetUniformLocation(sp->prog, "uM");
    sp->u_off = glGetUniformLocation(sp->prog, "uOff");
}

void qw_yuv_matrix(int enc, int full, float m[9], float off[3]) {
    double kr = enc == 0 ? 0.299 : enc == 2 ? 0.2627 : 0.2126;
    double kb = enc == 0 ? 0.114 : enc == 2 ? 0.0593 : 0.0722;
    double kg = 1 - kr - kb;
    double ys = full ? 1.0 : 255.0 / 219.0, cs = full ? 1.0 : 255.0 / 224.0;
    // rows R, G, B; columns Y, U(Cb), V(Cr); stored column-major for GLSL
    double r[3][3] = {
        { ys, 0, (2 - 2 * kr) * cs },
        { ys, -kb * (2 - 2 * kb) / kg * cs, -kr * (2 - 2 * kr) / kg * cs },
        { ys, (2 - 2 * kb) * cs, 0 },
    };
    for (int c = 0; c < 3; c++)
        for (int row = 0; row < 3; row++) m[c * 3 + row] = (float)r[row][c];
    off[0] = full ? 0.0f : 16.0f / 255.0f;
    off[1] = off[2] = 128.0f / 255.0f;
}

void qw_renderer_set_yuv(struct qw_renderer *r, int enc, int full) {
    qw_yuv_matrix(enc, full, r->yuv_m, r->yuv_off);
}

int qw_renderer_init(struct qw_renderer *r, char *err, size_t errlen) {
    memset(r, 0, sizeof *r);
    if (!(r->prog = program(VS, FS, err, errlen))) return -1;
    if (!(r->oprog = program(OVS, OFS, err, errlen))) return -1;
    glGenBuffers(QW_NPANEL, r->ovbo);
    r->u_tex = glGetUniformLocation(r->prog, "uTex");
    r->u_origin = glGetUniformLocation(r->prog, "uOrigin");
    r->u_inv = glGetUniformLocation(r->prog, "uInv");
    r->sp[QW_SRC_RGB].prog = r->prog;
    locate(&r->sp[QW_SRC_RGB]);
    const char *fs[QW_SRC_NFMT] = { NULL, FS_RGBF, FS_OES, FS_NV12, FS_I420 };
    for (int f = 1; f < QW_SRC_NFMT; f++) {
        char e[512];
        if ((r->sp[f].prog = program(VS2, fs[f], e, sizeof e))) locate(&r->sp[f]);
        else if (f != QW_SRC_OES) { snprintf(err, errlen, "%s", e); return -1; }
        else QW_WARN("no external-image sampler (%s): zero-copy video sources unavailable", e);
    }
    qw_renderer_set_yuv(r, 1, 0);
    glGenBuffers(QW_NPANEL, r->vbo);
    glGenBuffers(1, &r->ibo);
    return 0;
}

int qw_renderer_set_panel(struct qw_renderer *r, int k, const struct qw_mesh *m,
                          GLuint tex, int tex_x, int tex_w, int tex_h) {
    if (qw_renderer_set_mesh(r, k, m)) return -1;
    struct qw_source s = { QW_SRC_RGB, { tex, 0, 0 }, tex_x, tex_w, tex_h, 0, 0 };
    return qw_renderer_set_source(r, k, &s);
}

int qw_renderer_set_source(struct qw_renderer *r, int k, const struct qw_source *s) {
    if (s->fmt < 0 || s->fmt >= QW_SRC_NFMT || !r->sp[s->fmt].prog) return -1;
    r->src[k] = *s;
    r->tex[k] = s->tex[0];
    r->tex_x[k] = s->tex_x;
    r->tex_w[k] = s->tex_w;
    r->tex_h[k] = s->tex_h;
    return 0;
}

void qw_renderer_clear_panel(struct qw_renderer *r, int k) {
    r->src[k].fmt = QW_SRC_RGB;
    r->tex[k] = 0;
    r->nidx[k] = 0;
}

int qw_renderer_set_mesh(struct qw_renderer *r, int k, const struct qw_mesh *m) {
    int mw = m->mw, mh = m->mh, nidx = (mw - 1) * (mh - 1) * 6;
    uint32_t *idx = malloc((size_t)nidx * sizeof *idx);
    if (!idx) return -1;
    int q = 0;
    for (int j = 0; j < mh - 1; j++)
        for (int i = 0; i < mw - 1; i++) {
            uint32_t a = j * mw + i, b = a + 1, c = a + mw, d = c + 1;
            idx[q++] = a; idx[q++] = b; idx[q++] = d;
            idx[q++] = a; idx[q++] = d; idx[q++] = c;
        }
    // one index buffer per mesh size; panels normally share it, but keep it
    // simple: re-upload, the last panel's size must match the others
    for (int p = 0; p < QW_NPANEL; p++)
        if (p != k && r->nidx[p] && r->nidx[p] != nidx) { free(idx); return -1; }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)nidx * sizeof *idx, idx, GL_STATIC_DRAW);
    free(idx);
    glBindBuffer(GL_ARRAY_BUFFER, r->vbo[k]);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)mw * mh * 4 * sizeof(float), m->v, GL_STATIC_DRAW);
    r->nidx[k] = nidx;
    return glGetError() == GL_NO_ERROR ? 0 : -1;
}

void qw_renderer_draw(struct qw_renderer *r) {
    glViewport(0, 0, SW, SH);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r->ibo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    for (int k = 0; k < QW_NPANEL; k++) {
        if (!r->nidx[k]) continue;
        const struct qw_source *s = &r->src[k];
        const struct qw_src_prog *sp = &r->sp[s->fmt];
        glUseProgram(sp->prog);
        glViewport(0, SH - (k + 1) * QW_PH, QW_PW, QW_PH); // GL is bottom-up
        glUniform1i(sp->u_tex, 0);
        glActiveTexture(GL_TEXTURE0);
        if (s->fmt == QW_SRC_OES) glBindTexture(GL_TEXTURE_EXTERNAL_OES, r->tex[k]);
        else glBindTexture(GL_TEXTURE_2D, r->tex[k]);
        if (s->fmt == QW_SRC_NV12 || s->fmt == QW_SRC_I420) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, s->tex[1]);
            glUniform1i(sp->u_tex1, 1);
            if (s->fmt == QW_SRC_I420) {
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, s->tex[2]);
                glUniform1i(sp->u_tex2, 2);
            }
            glActiveTexture(GL_TEXTURE0);
            glUniformMatrix3fv(sp->u_m, 1, GL_FALSE, r->yuv_m);
            glUniform3fv(sp->u_off, 1, r->yuv_off);
        }
        if (sp->u_frame >= 0) glUniform2f(sp->u_frame, s->frame_w, s->frame_h);
        glUniform2f(sp->u_origin, r->tex_x[k], 0.0f);
        glUniform2f(sp->u_inv, 1.0f / r->tex_w[k], 1.0f / r->tex_h[k]);
        glBindBuffer(GL_ARRAY_BUFFER, r->vbo[k]);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void *)0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void *)8);
        glDrawElements(GL_TRIANGLES, r->nidx[k], GL_UNSIGNED_INT, 0);
    }
    int any = 0;
    for (int k = 0; k < QW_NPANEL; k++) any |= r->oline[k] + r->otri[k];
    if (!any) return;
    glUseProgram(r->oprog);
    glLineWidth(1.0f);
    for (int k = 0; k < QW_NPANEL; k++) {
        if (!r->oline[k] && !r->otri[k]) continue;
        glViewport(0, SH - (k + 1) * QW_PH, QW_PW, QW_PH);
        glBindBuffer(GL_ARRAY_BUFFER, r->ovbo[k]);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 20, (void *)0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 20, (void *)8);
        if (r->oline[k]) glDrawArrays(GL_LINES, 0, r->oline[k]);
        if (r->otri[k]) glDrawArrays(GL_TRIANGLES, r->oline[k], r->otri[k]);
    }
}

int qw_renderer_set_overlay(struct qw_renderer *r, int k, const float *v, int nline, int ntri) {
    r->oline[k] = r->otri[k] = 0;
    if (nline + ntri == 0) return 0;
    glBindBuffer(GL_ARRAY_BUFFER, r->ovbo[k]);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(nline + ntri) * 5 * sizeof(float), v, GL_DYNAMIC_DRAW);
    if (glGetError() != GL_NO_ERROR) return -1;
    r->oline[k] = nline;
    r->otri[k] = ntri;
    return 0;
}

void qw_renderer_readback(uint8_t *rgb) {
    uint8_t *px = malloc((size_t)SW * SH * 4);
    if (!px) return;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, SW, SH, GL_RGBA, GL_UNSIGNED_BYTE, px); // bottom-up
    for (int y = 0; y < QW_PH; y++)
        for (int x = 0; x < QW_FW; x++) {
            int k = x / QW_PW, by = SH - 1 - (k * QW_PH + y);
            memcpy(rgb + 3 * ((size_t)y * QW_FW + x), px + 4 * ((size_t)by * SW + x % QW_PW), 3);
        }
    free(px);
}

void qw_renderer_free(struct qw_renderer *r) {
    for (int f = 1; f < QW_SRC_NFMT; f++)
        if (r->sp[f].prog) glDeleteProgram(r->sp[f].prog);
    if (r->prog) glDeleteProgram(r->prog);
    if (r->oprog) glDeleteProgram(r->oprog);
    glDeleteBuffers(QW_NPANEL, r->ovbo);
    if (r->ibo) glDeleteBuffers(1, &r->ibo);
    glDeleteBuffers(QW_NPANEL, r->vbo);
    memset(r, 0, sizeof *r);
}
