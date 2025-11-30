/*
 * Dithering header.
 *
 * Bayer, clustered and threshold algorithms are available.
 *
 * Copyright © 2025 by Mark Dornbach
 *
 * Licensed under GNU GPL v3.
 */

#ifndef TPCL_DITHER_H
#define TPCL_DITHER_H

#include <pappl/pappl.h>

void dither_bayer16(pappl_dither_t dither);

void dither_clustered16(pappl_dither_t dither);

void dither_threshold16(pappl_dither_t dither, unsigned char level);

#endif // #define TPCL_DITHER_H
