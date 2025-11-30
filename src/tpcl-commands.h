/*
 * TPCL Commands Header
 *
 * TPCL v2 command generation for Toshiba TEC label printers.
 *
 * Copyright © 2025 by Mark Dornbach
 *
 * Licensed under GNU GPL v3.
 */

#ifndef TPCL_COMMANDS_H
#define TPCL_COMMANDS_H

#include <pappl/pappl.h>

/*
 * 'tpcl_cmd_label_size()' - Generate D command (label size definition)
 *
 * Format: {D<pitch>,<width>,<height>,<roll_width>|}
 *
 * Parameters:
 *   device      - Output device
 *   label_pitch - Label pitch in 0.1mm (height + gap)
 *   width       - Print width in 0.1mm
 *   height      - Print height in 0.1mm
 *   roll_width  - Roll width in 0.1mm
 *   job         - Job for logging (can be NULL)
 *   printer     - Printer for logging (can be NULL)
 *
 * Returns:
 *   Number of bytes written
 */

ssize_t tpcl_cmd_label_size(pappl_device_t *device, int label_pitch, int width,
			    int height, int roll_width, pappl_job_t *job,
			    pappl_printer_t *printer);

/*
 * 'tpcl_cmd_feed()' - Generate T command (feed label)
 *
 * Format: {T<sensor><cut><mode><speed><ribbon>|}
 *
 * Parameters:
 *   device       - Output device
 *   sensor_char  - Sensor type: 0=none, 1=reflective, 2=transmissive, 3=trans pre-print, 4=refl pre-print
 *   cut_char     - Cut selection: 0=non-cut, 1=cut
 *   mode_char    - Feed mode: C=batch, D=strip sensor valid, E=strip sensor ignored, F=partial-cut
 *   speed_char   - Feed speed: 0-9,A-F hex character
 *   ribbon_char  - Ribbon: 0=direct/no ribbon, 1=tt ribbon saving, 2=tt no ribbon saving
 *   job          - Job for logging (can be NULL)
 *   printer      - Printer for logging (can be NULL)
 *
 * Returns:
 *   Number of bytes written
 */

ssize_t tpcl_cmd_feed(pappl_device_t *device, char sensor_char, char cut_char,
		      char mode_char, char speed_char, char ribbon_char,
		      pappl_job_t *job, pappl_printer_t *printer);

/*
 * 'tpcl_cmd_position_adjust()' - Generate AX command (position fine adjustment)
 *
 * Format: {AX;<+/->feed,<+/->cut,<+/->backfeed|}
 *
 * Parameters:
 *   device         - Output device
 *   feed_adj       - Feed adjustment in 0.1mm (negative = forward, positive = backward)
 *   cut_adj        - Cut position adjustment in 0.1mm
 *   backfeed_adj   - Backfeed adjustment in 0.1mm
 *   job            - Job for logging (can be NULL)
 *   printer        - Printer for logging (can be NULL)
 *
 * Returns:
 *   Number of bytes written
 */

ssize_t tpcl_cmd_position_adjust(pappl_device_t *device, int feed_adj,
				 int cut_adj, int backfeed_adj,
				 pappl_job_t *job, pappl_printer_t *printer);

/*
 * 'tpcl_cmd_darkness_adjust()' - Generate AY command (print darkness adjustment)
 *
 * Format: {AY;<+/->darkness,<type>|}
 *
 * Parameters:
 *   device     - Output device
 *   darkness   - Darkness adjustment (negative = lighter, positive = darker)
 *   type_char  - Type: 0=standard adjustment
 *   job        - Job for logging (can be NULL)
 *   printer    - Printer for logging (can be NULL)
 *
 * Returns:
 *   Number of bytes written
 */

ssize_t tpcl_cmd_darkness_adjust(pappl_device_t *device, int darkness,
				 char type_char, pappl_job_t *job,
				 pappl_printer_t *printer);

/*
 * 'tpcl_cmd_clear_buffer()' - Generate C command (clear image buffer)
 *
 * Format: {C|}
 *
 * Parameters:
 *   device  - Output device
 *   job     - Job for logging (can be NULL)
 *   printer - Printer for logging (can be NULL)
 *
 * Returns:
 *   Number of bytes written
 */

ssize_t tpcl_cmd_clear_buffer(pappl_device_t *device, pappl_job_t *job,
			      pappl_printer_t *printer);

/*
 * 'tpcl_cmd_graphics_header()' - Generate SG command header (start graphics)
 *
 * Format: {SG;<x_origin>,<y_origin>,<width>,<height>,<mode>,
 * Note: Caller must send graphics data and closing |}
 *
 * Parameters:
 *   device     - Output device
 *   x_origin   - X origin in 0.1mm
 *   y_origin   - Y origin in 0.1mm
 *   width      - Width in dots
 *   height     - Height in dots (or resolution for TOPIX)
 *   gmode      - Graphics mode (0-6)
 *   job        - Job for logging (can be NULL)
 *   printer    - Printer for logging (can be NULL)
 *
 * Returns:
 *   Number of bytes written
 */

ssize_t tpcl_cmd_graphics_header(pappl_device_t *device, int x_origin,
				 int y_origin, unsigned int width,
				 unsigned int height, int gmode,
				 pappl_job_t *job, pappl_printer_t *printer);

/*
 * 'tpcl_cmd_issue_label()' - Generate XS command (execute print/issue label)
 *
 * Format: {XS;I,<copies>,<cut_interval><sensor><mode><speed><ribbon><rotation><response>|}
 *
 * Parameters:
 *   device       - Output device
 *   copies       - Number of copies (1-9999)
 *   cut_interval - Cut interval (0-100, 0=no cut)
 *   sensor_char  - Sensor type character
 *   mode_char    - Issue mode character
 *   speed_char   - Issue speed character
 *   ribbon_char  - Ribbon character
 *   rotation     - Tag rotation: 0=no rotation
 *   response     - Status response: 0=not needed
 *   job          - Job for logging (can be NULL)
 *   printer      - Printer for logging (can be NULL)
 *
 * Returns:
 *   Number of bytes written
 */

ssize_t tpcl_cmd_issue_label(pappl_device_t *device, int copies,
			     int cut_interval, char sensor_char, char mode_char,
			     char speed_char, char ribbon_char, char rotation,
			     char response, pappl_job_t *job,
			     pappl_printer_t *printer);

/*
 * 'tpcl_cmd_line()' - Generate LC command (draw line/rectangle)
 *
 * Format: {LC;<x1>,<y1>,<x2>,<y2>,<type>,<width>|}
 *
 * Parameters:
 *   device     - Output device
 *   x1         - Start X coordinate in 0.1mm
 *   y1         - Start Y coordinate in 0.1mm
 *   x2         - End X coordinate in 0.1mm
 *   y2         - End Y coordinate in 0.1mm
 *   type       - Shape type: 1=rectangle
 *   line_width - Line width in dots
 *   job        - Job for logging (can be NULL)
 *   printer    - Printer for logging (can be NULL)
 *
 * Returns:
 *   Number of bytes written
 */

ssize_t tpcl_cmd_line(pappl_device_t *device, int x1, int y1, int x2, int y2,
		      int type, int line_width, pappl_job_t *job,
		      pappl_printer_t *printer);

#endif // TPCL_COMMANDS_H
