/*
 * TPCL Commands Implementation
 *
 * TPCL v2 command generation for Toshiba TEC label printers.
 *
 * Copyright © 2025 by Mark Dornbach
 *
 * Licensed under GNU GPL v3.
 */

#include "tpcl-commands.h"
#include <stdio.h>


/*
 * 'tpcl_cmd_label_size()' - Generate D command (label size definition)
 */

ssize_t
tpcl_cmd_label_size(
  pappl_device_t           *device,
  int                      label_pitch,
  int                      width,
  int                      height,
  int                      roll_width,
  pappl_job_t              *job,
  pappl_printer_t          *printer
)
{
  char command[256];

  snprintf(
    command,
    sizeof(command),
    "{D%04d,%04d,%04d,%04d|}\n",
    label_pitch,
    width,
    height,
    roll_width
  );

  if (job)
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending label size command: %s", command);
  else if (printer)
    papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Sending label size command: %s", command);

  return papplDevicePuts(device, command);
}


/*
 * 'tpcl_cmd_feed()' - Generate T command (feed label)
 */

ssize_t
tpcl_cmd_feed(
  pappl_device_t           *device,
  char                     sensor_char,
  char                     cut_char,
  char                     mode_char,
  char                     speed_char,
  char                     ribbon_char,
  pappl_job_t              *job,
  pappl_printer_t          *printer
)
{
  char command[256];

  snprintf(
    command,
    sizeof(command),
    "{T%c%c%c%c%c|}\n",
    sensor_char,
    cut_char,
    mode_char,
    speed_char,
    ribbon_char
  );

  if (job)
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending feed command: %s", command);
  else if (printer)
    papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Sending feed command: %s", command);

  return papplDevicePuts(device, command);
}


/*
 * 'tpcl_cmd_position_adjust()' - Generate AX command (position fine adjustment)
 */

ssize_t
tpcl_cmd_position_adjust(
  pappl_device_t           *device,
  int                      feed_adj,
  int                      cut_adj,
  int                      backfeed_adj,
  pappl_job_t              *job,
  pappl_printer_t          *printer
)
{
  char command[256];

  snprintf(
    command,
    sizeof(command),
    "{AX;%c%03d,%c%03d,%c%02d|}\n",
    (feed_adj >= 0) ? '+' : '-', abs(feed_adj),
    (cut_adj >= 0) ? '+' : '-', abs(cut_adj),
    (backfeed_adj >= 0) ? '+' : '-', abs(backfeed_adj)
  );

  if (job)
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending position adjustment command: %s", command);
  else if (printer)
    papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Sending position adjustment command: %s", command);

  return papplDevicePuts(device, command);
}


/*
 * 'tpcl_cmd_darkness_adjust()' - Generate AY command (print darkness adjustment)
 */

ssize_t
tpcl_cmd_darkness_adjust(
  pappl_device_t           *device,
  int                      darkness,
  char                     type_char,
  pappl_job_t              *job,
  pappl_printer_t          *printer
)
{
  char command[256];

  snprintf(
    command,
    sizeof(command),
    "{AY;%c%02d,%c|}\n",
    (darkness >= 0) ? '+' : '-',
    abs(darkness),
    type_char
  );

  if (job)
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending darkness adjustment command: %s", command);
  else if (printer)
    papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Sending darkness adjustment command: %s", command);

  return papplDevicePuts(device, command);
}


/*
 * 'tpcl_cmd_clear_buffer()' - Generate C command (clear image buffer)
 */

ssize_t
tpcl_cmd_clear_buffer(
  pappl_device_t           *device,
  pappl_job_t              *job,
  pappl_printer_t          *printer
)
{
  if (job)
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending clear image buffer command: {C|}");
  else if (printer)
    papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Sending clear image buffer command: {C|}");

  return papplDevicePuts(device, "{C|}\n");
}


/*
 * 'tpcl_cmd_graphics_header()' - Generate SG command header (start graphics)
 */

ssize_t
tpcl_cmd_graphics_header(
  pappl_device_t           *device,
  int                      x_origin,
  int                      y_origin,
  unsigned int             width,
  unsigned int             height,
  int                      gmode,
  pappl_job_t              *job,
  pappl_printer_t          *printer
)
{
  char command[256];

  snprintf(
    command,
    sizeof(command),
    "{SG;%04d,%05d,%04u,%05u,%d,",
    x_origin,
    y_origin,
    width,
    height,
    gmode
  );

  if (job)
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending graphic command header (width, height, mode): %s", command);
  else if (printer)
    papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Sending graphic command header (width, height, mode): %s", command);

  return papplDevicePuts(device, command);
}


/*
 * 'tpcl_cmd_issue_label()' - Generate XS command (execute print/issue label)
 */

ssize_t
tpcl_cmd_issue_label(
  pappl_device_t           *device,
  int                      copies,
  int                      cut_interval,
  char                     sensor_char,
  char                     mode_char,
  char                     speed_char,
  char                     ribbon_char,
  char                     rotation,
  char                     response,
  pappl_job_t              *job,
  pappl_printer_t          *printer
)
{
  char command[256];

  snprintf(
    command,
    sizeof(command),
    "{XS;I,%04d,%03d%c%c%c%c%c%c|}\n",
    copies,
    cut_interval,
    sensor_char,
    mode_char,
    speed_char,
    ribbon_char,
    rotation,
    response
  );

  if (job)
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending issue label command: %s", command);
  else if (printer)
    papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Sending issue label command: %s", command);

  return papplDevicePuts(device, command);
}


/*
 * 'tpcl_cmd_line()' - Generate LC command (draw line/rectangle)
 */

ssize_t
tpcl_cmd_line(
  pappl_device_t           *device,
  int                      x1,
  int                      y1,
  int                      x2,
  int                      y2,
  int                      type,
  int                      line_width,
  pappl_job_t              *job,
  pappl_printer_t          *printer
)
{
  char command[256];

  snprintf(
    command,
    sizeof(command),
    "{LC;%04d,%04d,%04d,%04d,%d,%d|}\n",
    x1,
    y1,
    x2,
    y2,
    type,
    line_width
  );

  if (job)
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending LC command for box: %s", command);
  else if (printer)
    papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Sending LC command for box: %s", command);

  return papplDevicePuts(device, command);
}
