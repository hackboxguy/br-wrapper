// Built-in straight-grid test source (tune source grid): per panel a 1500x298
// texture with the regular 21x9 source grid the calibration refers to: grey
// grid lines, white 5x5 dots on the nodes and a border in the panel colour
// (green, red, blue, as in the reference test pattern). Warped, the dots must
// land on the calibration points, so the tuning does not depend on the app.
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
