/*
 * TPCL Compression Header
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

#ifndef TPCL_COMPRESSION_H
#define TPCL_COMPRESSION_H

#include <pappl/pappl.h>

/*
 * Graphics mode constants
 */

#define TEC_GMODE_NIBBLE_AND \
	0 // Raw nibble AND mode (4 dots/byte encoded in ASCII)
#define TEC_GMODE_HEX_AND 1 // Raw hex AND mode (8 dots/byte)
#define TEC_GMODE_TOPIX 3 // TOPIX compression (default, recommended)
#define TEC_GMODE_NIBBLE_OR \
	4 // Raw nibble OR mode (4 dots/byte encoded in ASCII)
#define TEC_GMODE_HEX_OR 5 // Raw hex OR mode (8 dots/byte)

/*
 * Compression buffer constants
 */

#define TPCL_COMP_BUFFER_MAX \
	0xFFFF // Maximum compression buffer size (65535 bytes)

/*
 * Opaque compression buffer handle
 */

typedef struct tpcl_compbuf_s tpcl_compbuf_t;

/*
 * 'tpcl_compbuf_create()' - Create compression buffers for TOPIX mode
 *
 * Allocates buffers needed for TOPIX compression: previous line and compression buffer.
 *
 * Parameters:
 *   line_bytes - Size of one raster line in bytes
 *   job        - Job for logging (can be NULL)
 *
 * Returns:
 *   Compression buffer handle, or NULL on allocation failure
 */

tpcl_compbuf_t *tpcl_compbuf_create(unsigned int line_bytes, pappl_job_t *job);

/*
 * 'tpcl_compbuf_delete()' - Free compression buffers
 *
 * Parameters:
 *   compbuf - Compression buffer handle to free
 */

void tpcl_compbuf_delete(tpcl_compbuf_t *compbuf);

/*
 * 'tpcl_compbuf_reset()' - Reset compression buffers for new page
 *
 * Zeros out all buffers and resets state for a new page.
 *
 * Parameters:
 *   compbuf - Compression buffer handle
 */

void tpcl_compbuf_reset(tpcl_compbuf_t *compbuf);

/*
 * 'tpcl_topix_compress_line()' - Compress one line using TOPIX algorithm
 *
 * TOPIX is a 3-level hierarchical compression that performs XOR
 * with the previous line and only transmits changed bytes.
 *
 * Parameters:
 *   compbuf   - Compression buffer handle
 *   line_data - Pointer to current line data to compress
 */

void tpcl_topix_compress_line(tpcl_compbuf_t *compbuf,
			      const unsigned char *line_data);

/*
 * 'tpcl_topix_get_buffer_used()' - Get number of bytes in compression buffer
 *
 * Parameters:
 *   compbuf - Compression buffer handle
 *
 * Returns:
 *   Number of bytes currently in compression buffer
 */

size_t tpcl_topix_get_buffer_used(tpcl_compbuf_t *compbuf);

/*
 * 'tpcl_topix_flush()' - Send TOPIX compressed data to printer and reset
 *
 * Outputs the SG command with accumulated compressed data, then resets buffers.
 *
 * Parameters:
 *   compbuf    - Compression buffer handle
 *   device     - Output device
 *   y_offset   - Y offset for this image in 0.1mm
 *   width_dots - Width in dots
 *   resolution - Resolution in DPI (150 or 300)
 *   gmode      - Graphics mode (should be TEC_GMODE_TOPIX)
 *
 * Returns:
 *   Number of bytes written to device, or -1 on error
 */

ssize_t tpcl_topix_flush(tpcl_compbuf_t *compbuf, pappl_device_t *device,
			 int y_offset, unsigned int width_dots,
			 unsigned int resolution, int gmode);

#endif // TPCL_COMPRESSION_H
