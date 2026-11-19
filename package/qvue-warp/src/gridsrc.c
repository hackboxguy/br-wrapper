// Built-in straight-grid test source (tune source grid): per panel a 1500x298
// texture with the regular 21x9 source grid the calibration refers to, made to
// judge straightness by eye: light grey grid lines 3 px wide, white 5x5 dots on
// the nodes, a 3 px border and a 5 px centre cross in the panel colour (green,
// red, blue, as in the reference test pattern), orange corner-to-corner
// diagonals and an orange circle round the centre (curvature and shear show at
// once). Warped, the dots must land on the calibration points, so the tuning
// does not depend on the app. While it is the picture the tuning overlay draws
// only its handle markers (its lines would lie on the grid lines).
// Only the dots are white: lines and colours stay at or below 150 in at least
// one channel, so dot detection (any channel > 150 in all three) sees dots only.
// A label "PANEL n" in the panel colour sits in the second cell from the top
// left; it is part of the picture, so with mirror_x / mirror_y it shows up
// mirrored (and on the other panel), which makes the mirror state visible.
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "gridsrc.h"

#define GW 21
#define GH 9

static void px(uint8_t *img, int x, int y, const uint8_t c[3]) {
    if (x < 0 || y < 0 || x >= QW_PW || y >= QW_PH) return;
    uint8_t *p = img + 4 * ((size_t)y * QW_PW + x);
    p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; p[3] = 255;
}

// 5x7 glyphs, one byte per row, bit 4 = left column
static const struct { char c; uint8_t row[7]; } font[] = {
    { 'P', { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 } },
    { 'A', { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 } },
    { 'N', { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 } },
    { 'E', { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f } },
    { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f } },
    { '1', { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e } },
    { '2', { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f } },
    { '3', { 0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e } },
};

static void text(uint8_t *img, int x, int y, int scale, const char *t, const uint8_t c[3]) {
    for (; *t; t++, x += 6 * scale)
        for (size_t g = 0; g < sizeof font / sizeof font[0]; g++) {
            if (font[g].c != *t) continue;
            for (int r = 0; r < 7; r++)
                for (int b = 0; b < 5; b++)
                    if (font[g].row[r] & (0x10 >> b))
                        for (int sy = 0; sy < scale; sy++)
                            for (int sx = 0; sx < scale; sx++) px(img, x + b * scale + sx, y + r * scale + sy, c);
        }
}

static int node_x(int i) { return (int)(i * (QW_PW - 1) / (double)(GW - 1) + 0.5); }
static int node_y(int j) { return (int)(j * (QW_PH - 1) / (double)(GH - 1) + 0.5); }

// a line of width w (odd) from (x0, y0) to (x1, y1)
static void line(uint8_t *img, double x0, double y0, double x1, double y1, int w, const uint8_t c[3]) {
    int n = (int)ceil(fmax(fabs(x1 - x0), fabs(y1 - y0))) * 2 + 1;
    for (int s = 0; s <= n; s++) {
        double t = (double)s / n;
        int x = (int)lround(x0 + t * (x1 - x0)), y = (int)lround(y0 + t * (y1 - y0));
        for (int dy = -w / 2; dy <= w / 2; dy++)
            for (int dx = -w / 2; dx <= w / 2; dx++) px(img, x + dx, y + dy, c);
    }
}

void gridsrc_image(int k, uint8_t *img) {
    static const uint8_t border[QW_NPANEL][3] = { { 0, 255, 0 }, { 255, 0, 0 }, { 0, 80, 255 } };
    static const uint8_t grey[3] = { 150, 150, 150 }, white[3] = { 255, 255, 255 }, orange[3] = { 255, 140, 0 };
    const int W = QW_PW - 1, H = QW_PH - 1;
    memset(img, 0, (size_t)QW_PW * QW_PH * 4);
    for (size_t i = 0; i < (size_t)QW_PW * QW_PH; i++) img[4 * i + 3] = 255;
    for (int i = 0; i < GW; i++) line(img, node_x(i), 0, node_x(i), H, 3, grey);
    for (int j = 0; j < GH; j++) line(img, 0, node_y(j), W, node_y(j), 3, grey);
    line(img, 0, 0, W, H, 3, orange);
    line(img, 0, H, W, 0, 3, orange);
    double cx = node_x(GW / 2), cy = node_y(GH / 2), r = node_y(GH / 2) - node_y(1);
    for (int s = 0; s < 720; s++) {
        double a0 = s * M_PI / 360, a1 = (s + 1) * M_PI / 360;
        line(img, cx + r * cos(a0), cy + r * sin(a0), cx + r * cos(a1), cy + r * sin(a1), 3, orange);
    }
    line(img, cx, cy - 2 * (node_y(1) - node_y(0)), cx, cy + 2 * (node_y(1) - node_y(0)), 5, border[k]);
    line(img, cx - 2 * (node_x(1) - node_x(0)), cy, cx + 2 * (node_x(1) - node_x(0)), cy, 5, border[k]);
    line(img, 1, 1, W - 1, 1, 3, border[k]);
    line(img, 1, H - 1, W - 1, H - 1, 3, border[k]);
    line(img, 1, 1, 1, H - 1, 3, border[k]);
    line(img, W - 1, 1, W - 1, H - 1, 3, border[k]);
    // between grid rows 1 and 2 (y 37..74), clear of the dots
    char label[] = "PANEL 1";
    label[6] = (char)('1' + k);
    text(img, 81, 45, 3, label, border[k]);
    for (int j = 0; j < GH; j++)
        for (int i = 0; i < GW; i++)
            for (int dy = -2; dy <= 2; dy++)
                for (int dx = -2; dx <= 2; dx++) px(img, node_x(i) + dx, node_y(j) + dy, white);
}

int gridsrc_create(GLuint tex[QW_NPANEL]) {
    uint8_t *img = malloc((size_t)QW_PW * QW_PH * 4);
    if (!img) return -1;
    glGenTextures(QW_NPANEL, tex);
    for (int k = 0; k < QW_NPANEL; k++) {
        gridsrc_image(k, img);
        glBindTexture(GL_TEXTURE_2D, tex[k]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, QW_PW, QW_PH, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    }
    free(img);
    return glGetError() == GL_NO_ERROR ? 0 : -1;
}

void gridsrc_free(GLuint tex[QW_NPANEL]) {
    if (tex[0]) glDeleteTextures(QW_NPANEL, tex);
    memset(tex, 0, QW_NPANEL * sizeof *tex);
}
