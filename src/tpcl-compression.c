/*
 * TPCL Compression Implementation
 *
 * Compression algorithms and buffer management for Toshiba TEC TPCL printers.
 * Supports TOPIX 3-level hierarchical XOR compression.
 *
 * Copyright © 2025 by Mark Dornbach
 * Copyright © 2010 by Sam Lown
 * Copyright © 2009 by Patrick Kong
 * Copyright © 2001-2007 by Easy Software Products
 *
 * Licensed under GNU GPL v3.
 */

#include "tpcl-compression.h"
#include <stdlib.h>
#include <string.h>


/*
 * Private compression buffer structure
 */

struct tpcl_compbuf_s {
  unsigned int             line_bytes;                   // Size of one line in bytes
  unsigned char            *last_buffer;                 // Previous line buffer (for XOR comparison)
  unsigned char            *comp_buffer;                 // Compression buffer (65KB max)
  unsigned char            *comp_buffer_ptr;             // Current position in comp_buffer
};


/*
 * 'tpcl_compbuf_create()' - Create compression buffers for TOPIX mode
 */

tpcl_compbuf_t*
tpcl_compbuf_create(
  unsigned int             line_bytes,
  pappl_job_t              *job
)
{
  tpcl_compbuf_t *compbuf = (tpcl_compbuf_t *)calloc(1, sizeof(tpcl_compbuf_t));

  if (!compbuf)
  {
    if (job)
      papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Failed to allocate compression buffer structure");
    return NULL;
  }

  compbuf->line_bytes  = line_bytes;
  compbuf->last_buffer = (unsigned char *)calloc(line_bytes, 1);
  compbuf->comp_buffer = (unsigned char *)calloc(0xFFFF, 1);

  if (!compbuf->last_buffer || !compbuf->comp_buffer)
  {
    if (job)
      papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Failed to allocate TOPIX compression buffers");

    tpcl_compbuf_delete(compbuf);
    return NULL;
  }

  compbuf->comp_buffer_ptr = compbuf->comp_buffer;

  if (job)
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "TOPIX buffers allocated: line=%u bytes, comp=65535 bytes", line_bytes);

  return compbuf;
}


/*
 * 'tpcl_compbuf_delete()' - Free compression buffers
 */

void
tpcl_compbuf_delete(
  tpcl_compbuf_t           *compbuf
)
{
  if (compbuf)
  {
    if (compbuf->last_buffer) free(compbuf->last_buffer);
    if (compbuf->comp_buffer) free(compbuf->comp_buffer);
    free(compbuf);
  }
}


/*
 * 'tpcl_compbuf_reset()' - Reset compression buffers for new page
 */

void
tpcl_compbuf_reset(
  tpcl_compbuf_t           *compbuf
)
{
  if (compbuf)
  {
    memset(compbuf->last_buffer, 0, compbuf->line_bytes);
    memset(compbuf->comp_buffer, 0, 0xFFFF);
    compbuf->comp_buffer_ptr = compbuf->comp_buffer;
  }
}


/*
 * 'tpcl_topix_compress_line()' - Compress one line using TOPIX algorithm
 *
 * TOPIX is a 3-level hierarchical compression that performs XOR
 * with the previous line and only transmits changed bytes.
 */

void
tpcl_topix_compress_line(
  tpcl_compbuf_t           *compbuf,
  const unsigned char      *line_data
)
{
  int                      i = 0;                        // Index into buffer
  int                      l1, l2, l3;                   // Current positions in line
  int                      max = 8 * 9 * 9;              // Max number of items per line
  unsigned int             width;                        // Max width of the line
  unsigned char            line[8][9][9] = {{{0}}};      // Current line (L1 x L2 x L3)
  unsigned char            cl1, cl2, cl3;                // Current characters
  unsigned char            xor;                          // Current XORed character
  unsigned char            *ptr;                         // Pointer into the Compressed Line Buffer

  width = compbuf->line_bytes;

  // Perform XOR with previous line for differential compression in a 3-level index structure
  cl1 = 0;
  for (l1 = 0; l1 <= 7 && i < width; l1++)
  {
    cl2 = 0;
    for (l2 = 1; l2 <= 8 && i < width; l2++)
    {
      cl3 = 0;
      for (l3 = 1; l3 <= 8 && i < width; l3++, i++)      // Careful, this loop increments two variables!
      {
        xor = line_data[i] ^ compbuf->last_buffer[i];
        line[l1][l2][l3] = xor;
        if (xor > 0)
        {
          // Mark that this byte has changed
          cl3 |= (1 << (8 - l3));
        }
      }
      line[l1][l2][0] = cl3;
      if (cl3 != 0)
      {
        cl2 |= (1 << (8 - l2));
      }
    }
    line[l1][0][0] = cl2;
    if (cl2 != 0)
    {
      cl1 |= (1 << (7 - l1));
    }
  }

  // Always add L1 index byte to beginning of compressed buffer section (line) and move pointer one step forward
  *compbuf->comp_buffer_ptr = cl1;
  compbuf->comp_buffer_ptr++;

  // Copy only non-zero bytes to compressed buffer
  if (cl1 > 0)
  {
    ptr = &line[0][0][0];
    for (i = 0; i < max; i++)
    {
      if (*ptr != 0)
      {
        *compbuf->comp_buffer_ptr = *ptr;
        compbuf->comp_buffer_ptr++;
      }
      ptr++;
    }
  }

  // Copy current line to last_buffer for next iteration
  memcpy(compbuf->last_buffer, line_data, width);
}


/*
 * 'tpcl_topix_get_buffer_used()' - Get number of bytes in compression buffer
 */

size_t
tpcl_topix_get_buffer_used(
  tpcl_compbuf_t           *compbuf
)
{
  return (size_t)(compbuf->comp_buffer_ptr - compbuf->comp_buffer);
}


/*
 * 'tpcl_topix_flush()' - Send TOPIX compressed data to printer and reset
 */

ssize_t
tpcl_topix_flush(
  tpcl_compbuf_t           *compbuf,
  pappl_device_t           *device,
  int                      y_offset,
  unsigned int             width_dots,
  unsigned int             resolution,
  int                      gmode
)
{
  char                     command[256];
  unsigned short           len, belen;                   // Buffer length and big endian buffer length
  ssize_t                  bytes_written = 0;

  len   = (unsigned short)(compbuf->comp_buffer_ptr - compbuf->comp_buffer);
  belen = (len << 8) | (len >> 8);                       // Convert length to big-endian (network byte order)

  if (len == 0)
    return bytes_written;

  // Send SG command with compressed data
  snprintf(
    command,
    sizeof(command),
    "{SG;0000,%05d,%04u,%05u,%d,",
                                                         // x_origin in 0.1mm
    y_offset,                                            // y_origin in 0.1mm
    width_dots,                                          // width_dots
    resolution,                                          // in TOPIX mode: Resolution of graphic data (150 or 300 dpi)
    gmode                                                // graphics mode
  );

  bytes_written += papplDevicePuts (device, command);
  bytes_written += papplDeviceWrite(device, &belen, 2);                   // Total length of graphic data
  bytes_written += papplDeviceWrite(device, compbuf->comp_buffer, len);   // Compressed data
  bytes_written += papplDevicePuts (device, "|}\n");

  // Reset buffers and pointers
  memset(compbuf->comp_buffer, 0, 0xFFFF);
  memset(compbuf->last_buffer, 0, compbuf->line_bytes);
  compbuf->comp_buffer_ptr = compbuf->comp_buffer;

  return bytes_written;
}
