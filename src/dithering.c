/*
 * Helper file for creating dithering matrices. 
 *
 * Bayer, clustered and threshold algorithms are available.
 *
 * Copyright © 2025 by Mark Dornbach
 *
 * Licensed under GNU GPL v3.
 */

#include <pappl/pappl.h>
#include <math.h>


/*
 * Dispersed ordered (classic 16×16 Bayer)
 * When to use: Graphics or photos presets. Fine grain; good tone smoothness.
 */

void dither_bayer16(pappl_dither_t dither)
{  
  // Start with the 2×2 Bayer seed (in “rank” units)
  uint16_t M[16][16];
  uint16_t m2[2][2] = { {0, 2}, {3, 1} };

  // Build up: M_{2n} = [4*M + [0 2;3 1]] in each quadrant
  // First copy 2x2 into M
  for (int y=0; y<2; y++)
    for (int x=0; x<2; x++)
      M[y][x] = m2[y][x];

  int n = 2;
  while (n < 16)
  {
    for (int y=0; y<n; y++)
    {
      for (int x=0; x<n; x++)
      {
        uint16_t v = M[y][x] * 4;
        M[y][x]           = v + 0; // TL
        M[y][x + n]       = v + 2; // TR
        M[y + n][x]       = v + 3; // BL
        M[y + n][x + n]   = v + 1; // BR
      }
    }
    n *= 2;
  }

  // Scale ranks [0..255] to byte thresholds [0..255]
  // (There are 256 unique ranks in 16×16)
  for (int y=0; y<16; y++)
    for (int x=0; x<16; x++)
      dither[y][x] = (unsigned char)M[y][x]; // already 0..255
}


/*
 * Clustered ordered
 * When to use: Barcode, text-safe or solids preset for ordered, edge-friendly clustering. 
 */

void dither_clustered16(pappl_dither_t dither)
{
  struct P { int x,y; float w; } pts[16*16];
  int k = 0;

  // Tile is 16×16; choose a screen center (8,8). Weight = distance^2 to center.
  for (int y=0; y<16; y++)
    for (int x=0; x<16; x++)
    {
      float dx = (x + 0.5f) - 8.0f;
      float dy = (y + 0.5f) - 8.0f;
      pts[k++] = (struct P){x,y, dx*dx + dy*dy};
    }

  // Sort by weight ascending (center first)
  for (int i=0;i<256;i++)
    for (int j=i+1;j<256;j++)
      if (pts[j].w < pts[i].w) { struct P t = pts[i]; pts[i]=pts[j]; pts[j]=t; }

  // Assign thresholds 0..255 by rank
  for (int i=0;i<256;i++)
    dither[pts[i].y][pts[i].x] = (unsigned char)i;
}


/*
 * Threshold-only (no dither)
 * When to use: Barcodes, small text with the crispest edges and most predictable bar growth.
 * Use 'level' to set the threshold, use 128 as default.
 */

void dither_threshold16(pappl_dither_t dither, unsigned char level)
{
  for (int y=0; y<16; y++)
    for (int x=0; x<16; x++)
      dither[y][x] = level;
}