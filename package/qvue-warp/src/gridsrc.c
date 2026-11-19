// Built-in straight-grid test source (tune source grid): per panel a 1500x298
// texture with the regular 21x9 source grid the calibration refers to: grey
// grid lines, white 5x5 dots on the nodes and a border in the panel colour
// (green, red, blue, as in the reference test pattern). Warped, the dots must
// land on the calibration points, so the tuning does not depend on the app.
// A label "PANEL n" in the panel colour sits in the second cell from the top
// left; it is part of the picture, so with mirror_x / mirror_y it shows up
// mirrored (and on the other panel), which makes the mirror state visible.
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

void gridsrc_image(int k, uint8_t *img) {
    static const uint8_t border[QW_NPANEL][3] = { { 0, 255, 0 }, { 255, 0, 0 }, { 0, 80, 255 } };
    static const uint8_t grey[3] = { 110, 110, 110 }, white[3] = { 255, 255, 255 };
    memset(img, 0, (size_t)QW_PW * QW_PH * 4);
    for (size_t i = 0; i < (size_t)QW_PW * QW_PH; i++) img[4 * i + 3] = 255;
    for (int i = 0; i < GW; i++) {
        int x = (int)(i * (QW_PW - 1) / (double)(GW - 1) + 0.5);
        for (int y = 0; y < QW_PH; y++) px(img, x, y, grey);
    }
    for (int j = 0; j < GH; j++) {
        int y = (int)(j * (QW_PH - 1) / (double)(GH - 1) + 0.5);
        for (int x = 0; x < QW_PW; x++) px(img, x, y, grey);
    }
    for (int x = 0; x < QW_PW; x++) { px(img, x, 0, border[k]); px(img, x, QW_PH - 1, border[k]); }
    for (int y = 0; y < QW_PH; y++) { px(img, 0, y, border[k]); px(img, QW_PW - 1, y, border[k]); }
    // between grid rows 1 and 2 (y 37..74), clear of the dots
    char label[] = "PANEL 1";
    label[6] = (char)('1' + k);
    text(img, 81, 45, 3, label, border[k]);
    for (int j = 0; j < GH; j++)
        for (int i = 0; i < GW; i++) {
            int x = (int)(i * (QW_PW - 1) / (double)(GW - 1) + 0.5);
            int y = (int)(j * (QW_PH - 1) / (double)(GH - 1) + 0.5);
            for (int dy = -2; dy <= 2; dy++)
                for (int dx = -2; dx <= 2; dx++) px(img, x + dx, y + dy, white);
        }
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
