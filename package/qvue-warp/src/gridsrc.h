#ifndef QVUE_WARPD_GRIDSRC_H
#define QVUE_WARPD_GRIDSRC_H

#include <stdint.h>

#include "qvuewarp.h"

// RGBA 1500x298 image of panel k's straight test grid
void gridsrc_image(int k, uint8_t *img);
// one GL texture per panel (context current)
int gridsrc_create(GLuint tex[QW_NPANEL]);
void gridsrc_free(GLuint tex[QW_NPANEL]);

#endif
