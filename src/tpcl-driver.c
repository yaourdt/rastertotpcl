/*
 * TPCL Driver Implementation
 *
 * Printer driver for Toshiba TEC label printers supporting TPCL v2.
 *
 * Copyright © 2020-2025 by Mark Dornbach
 * Copyright © 2010 by Sam Lown
 * Copyright © 2009 by Patrick Kong
 * Copyright © 2001-2007 by Easy Software Products
 *
 * Licensed under GNU GPL v3.
 */

#include "tpcl-driver.h"
#include "tpcl-state.h"
#include "tpcl-compression.h"
#include "tpcl-commands.h"
#include "tpcl-ipp-utils.h"
#include "tpcl-config.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * Job data structure
 */

typedef struct {
  int                      gmode;                        // Graphics mode (TOPIX, hex, nibble)
  int                      print_width;                  // Effective print width (0.1mm)
  int                      print_height;                 // Effective print height (0.1mm)
  int                      label_pitch;                  // Label pitch = print height + label gap
  int                      roll_width;                   // Roll width
  size_t                   buffer_len;                   // Length of line buffer as sent to printer
  unsigned char            *buffer;                      // Current line buffer
  tpcl_compbuf_t           *compbuf;                     // Compression buffers (for TOPIX)
  int                      y_offset;                     // Y offset for next image in 0.1mm (for TOPIX)
} tpcl_job_t;


/*
 * Raster printing callbacks.
 */

bool tpcl_driver_cb(
  pappl_system_t           *system,
  const char               *driver_name,
  const char               *device_uri,
  const char               *device_id,
  pappl_pr_driver_data_t   *driver_data,
  ipp_t                    **driver_attrs,
  void                     *data
);

bool tpcl_status_cb(
  pappl_printer_t          *printer
);

void tpcl_identify_cb(
  pappl_printer_t          *printer, 
  pappl_identify_actions_t actions, 
  const char               *message
);

bool tpcl_print_cb(
  pappl_job_t              *job, 
  pappl_pr_options_t       *options, 
  pappl_device_t           *device
);

bool tpcl_rstartjob_cb(
  pappl_job_t              *job,
  pappl_pr_options_t       *options,
  pappl_device_t           *device
);

bool tpcl_rstartpage_cb(
  pappl_job_t              *job,
  pappl_pr_options_t       *options,
  pappl_device_t           *device,
  unsigned                 page
);

bool tpcl_rwriteline_cb(
  pappl_job_t              *job,
  pappl_pr_options_t       *options,
  pappl_device_t           *device,
  unsigned                 y,
  const unsigned char      *line
);

bool tpcl_rendpage_cb(
  pappl_job_t              *job,
  pappl_pr_options_t       *options,
  pappl_device_t           *device,
  unsigned                 page
);

bool tpcl_rendjob_cb(
  pappl_job_t              *job,
  pappl_pr_options_t       *options,
  pappl_device_t           *device
);

const char* tpcl_testpage_cb(
  pappl_printer_t          *printer,
  char                     *buffer,
  size_t                   bufsize
);

void tpcl_delete_cb(
  pappl_printer_t          *printer,
  pappl_pr_driver_data_t   *data
);


/*
 * Support functions
 */

static void tpcl_free_job_buffers(
  pappl_job_t              *job,
  tpcl_job_t               *tpcl_job
);


/*
 * 'tpcl_driver_cb()' - Main driver callback
 *
 * Configures the printer driver capabilities and callbacks.
 */

bool
tpcl_driver_cb(
  pappl_system_t           *system,                      // System config
  const char               *driver_name,                 // Driver name
  const char               *device_uri,                  // Device URI
  const char               *device_id,                   // IEEE-1284 device ID
  pappl_pr_driver_data_t   *driver_data,                 // Driver data
  ipp_t                    **driver_attrs,               // IPP driver attributes
  void                     *data                         // Context (not used)
)
{
  (void)data;

  //
  // Set callbacks
  //
  driver_data->status_cb     = tpcl_status_cb;           // Printer status callback
  driver_data->identify_cb   = tpcl_identify_cb;         // Identify-Printer callback (feed one blank label)
  driver_data->printfile_cb  = tpcl_print_cb;            // Print (raw) file callback
  driver_data->rstartjob_cb  = tpcl_rstartjob_cb;        // Start raster job callback
  driver_data->rstartpage_cb = tpcl_rstartpage_cb;       // Start raster page callback
  driver_data->rwriteline_cb = tpcl_rwriteline_cb;       // Write raster line callback
  driver_data->rendpage_cb   = tpcl_rendpage_cb;         // End raster page callback
  driver_data->rendjob_cb    = tpcl_rendjob_cb;          // End raster job callback
  driver_data->testpage_cb   = tpcl_testpage_cb;         // Test page print callback
  driver_data->delete_cb     = tpcl_delete_cb;           // Printer deletion callback

  // Setup vendor options
  if (!tpcl_setup_vendor_options(driver_data, driver_attrs))
    return false;

  // Setup common model-agnostic driver options
  if (!tpcl_setup_driver_common(driver_data, driver_attrs))
    return false;

  // Setup model-specific driver options
  if (!tpcl_setup_driver_model(system, driver_name, driver_data, driver_attrs))
    return false;

  return true;
}


/*
 * 'tpcl_status_cb()' - Get printer status
 *
 * Queries the printer status and evaluates the response:
 *   1. {WS|} -> Status request command
 * 
 * Returns true if printer is ready, false if error condition exists.
 */

bool
tpcl_status_cb(
  pappl_printer_t          *printer
)
{
  pappl_device_t           *device;                      // Printer device connection
  unsigned char            status[256];                  // Status response buffer
  ssize_t                  bytes;                        // Bytes read from printer
  bool                     printer_ready = false;        // Printer status flag

  // Open connection to the printer device
  device = papplPrinterOpenDevice(printer);
  if (!device)
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Failed to open device connection for status query");
    return printer_ready;
  }

  // Send status query command
  papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Status query sent, waiting for response...");
  papplDevicePuts(device, "{WS|}\n");
  papplDeviceFlush(device);

  // Poll for response with timeout (max. 20ms according to documentation)
  int poll_attempts = 0;
  const int max_attempts = 22;                           // Maximum polling attempts
  const int poll_interval_us = 1000;                     // 1ms between attempts

  bytes = 0;
  while (poll_attempts < max_attempts)
  {
    bytes = papplDeviceRead(device, status, sizeof(status));
    if (bytes != 0) break;                               // Data received, exit polling loop
    usleep(poll_interval_us);
    poll_attempts++;
  }

  // Check if response is valid
  if (bytes < 0)
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Error reading status response (error code: %ld)", (long)bytes);
    papplPrinterCloseDevice(printer);
    return printer_ready;
  }
  else if (bytes == 0)
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Timeout waiting for status response (%dms)", poll_attempts);
    papplPrinterCloseDevice(printer);
    return printer_ready;
  }

  // Expected format: 
  //       1 -> SOH
  //       2 -> STX
  //    3, 4 -> Status
  //       5 -> Status requested by flag
  //   6 - 9 -> Remaining number of labels to be issued
  //  10, 11 -> Length
  // 12 - 16 -> Free space receive buffer
  // 17 - 21 -> Receive buffer total capacity
  //      22 -> CR
  //      23 -> LF

  // Validate response format
  if (bytes >= 13 && status[0] == 0x01 && status[1] == 0x02)
  {
    // Extract status code
    char status_code[3];
    status_code[0] = status[2];
    status_code[1] = status[3];
    status_code[2] = '\0';
    papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Status response: '%s' after %dms", status_code, poll_attempts);

    // Check status code against documented values: "00"=ready, "02"=operating, "40"=print succeeded, "41"=feed succeeded
    if (strcmp(status_code, "00") == 0 || strcmp(status_code, "02") == 0 ||
        strcmp(status_code, "40") == 0 || strcmp(status_code, "41") == 0)
    {
      printer_ready = true;
      papplLogPrinter(printer, PAPPL_LOGLEVEL_INFO, "Printer ready (status: %s)", status_code);
      // Clear all error reasons when printer is ready
      papplPrinterSetReasons(printer, PAPPL_PREASON_NONE, PAPPL_PREASON_DEVICE_STATUS);
    }
    else
    {
      // Log specific error conditions based on status code and update PAPPL reasons
      if (strcmp(status_code, "01") == 0)
      {
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Top cover open");
        papplPrinterSetReasons(printer, PAPPL_PREASON_COVER_OPEN, PAPPL_PREASON_DEVICE_STATUS);
      }
      else if (strcmp(status_code, "03") == 0)
      {
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Exclusively accessed by other host");
        papplPrinterSetReasons(printer, PAPPL_PREASON_OTHER, PAPPL_PREASON_DEVICE_STATUS);
      }
      else if (strcmp(status_code, "04") == 0)
      {
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Paused");
        papplPrinterSetReasons(printer, PAPPL_PREASON_OTHER, PAPPL_PREASON_DEVICE_STATUS);
      }
      else if (strcmp(status_code, "05") == 0)
      {
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Waiting for stripping");
        papplPrinterSetReasons(printer, PAPPL_PREASON_OTHER, PAPPL_PREASON_DEVICE_STATUS);
      }
      else if (strcmp(status_code, "06") == 0)
      {
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Command error");
        papplPrinterSetReasons(printer, PAPPL_PREASON_OTHER, PAPPL_PREASON_DEVICE_STATUS);
      }
      else if (strcmp(status_code, "07") == 0)
      {
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "RS-232C error");
        papplPrinterSetReasons(printer, PAPPL_PREASON_OTHER, PAPPL_PREASON_DEVICE_STATUS);
      }
      else if (strcmp(status_code, "11") == 0)
      {
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Paper jam");
        papplPrinterSetReasons(printer, PAPPL_PREASON_MEDIA_JAM, PAPPL_PREASON_DEVICE_STATUS);
      }
      else if (strcmp(status_code, "12") == 0)
      {
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Paper jam at cutter");
        papplPrinterSetReasons(printer, PAPPL_PREASON_MEDIA_JAM, PAPPL_PREASON_DEVICE_STATUS);
      }
      else if (strcmp(status_code, "13") == 0)
      {
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "The label has run out");
        papplPrinterSetReasons(printer, PAPPL_PREASON_MEDIA_EMPTY, PAPPL_PREASON_DEVICE_STATUS);
      }
      else if (strcmp(status_code, "15") == 0)
      {
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Feed attempt while cover open");
        papplPrinterSetReasons(printer, PAPPL_PREASON_COVER_OPEN, PAPPL_PREASON_DEVICE_STATUS);
      }
      else if (strcmp(status_code, "16") == 0)
      {
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Steeping motor overheat");
        papplPrinterSetReasons(printer, PAPPL_PREASON_OTHER, PAPPL_PREASON_DEVICE_STATUS);
      }
      else if (strcmp(status_code, "18") == 0)
      {
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Thermal head overheat");
        papplPrinterSetReasons(printer, PAPPL_PREASON_OTHER, PAPPL_PREASON_DEVICE_STATUS);
      }
      else if (strcmp(status_code, "21") == 0)
      {
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "The ribbon has run out");
        papplPrinterSetReasons(printer, PAPPL_PREASON_MARKER_SUPPLY_EMPTY, PAPPL_PREASON_DEVICE_STATUS);
      }
      else if (strcmp(status_code, "23") == 0)
      {
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Print succeeded. The label has run out");
        papplPrinterSetReasons(printer, PAPPL_PREASON_MEDIA_EMPTY, PAPPL_PREASON_DEVICE_STATUS);
      }
      else if (strcmp(status_code, "50") == 0)
      {
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "SD card write error");
        papplPrinterSetReasons(printer, PAPPL_PREASON_OTHER, PAPPL_PREASON_DEVICE_STATUS);
      }
      else if (strcmp(status_code, "51") == 0)
      {
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "SD card format error");
        papplPrinterSetReasons(printer, PAPPL_PREASON_OTHER, PAPPL_PREASON_DEVICE_STATUS);
      }
      else if (strcmp(status_code, "54") == 0)
      {
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "SD card full");
        papplPrinterSetReasons(printer, PAPPL_PREASON_SPOOL_AREA_FULL, PAPPL_PREASON_DEVICE_STATUS);
      }
      else if (strcmp(status_code, "55") == 0)
      {
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "PC command mode / initialize SD / EEPROM error");
        papplPrinterSetReasons(printer, PAPPL_PREASON_OTHER, PAPPL_PREASON_DEVICE_STATUS);
      }
      else
      {
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Unknown status code: %s", status_code);
        papplPrinterSetReasons(printer, PAPPL_PREASON_OTHER, PAPPL_PREASON_DEVICE_STATUS);
      }
    }
  }
  else
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Invalid status response format (received %ld bytes)", (long)bytes);
  }

  // Close device connection
  papplPrinterCloseDevice(printer);

  return (printer_ready);
}


/*
 * 'tpcl_identify_cb()' - Identify printer
 *
 * Feeds one label using the following commands:
 *   1. {Daaaa,bbbb,cccc,dddd|} -> Label size definition
 *   2. {Tabcde|} -> Feed label
 */

void tpcl_identify_cb(
  pappl_printer_t          *printer,
  pappl_identify_actions_t actions,
  const char               *message
)
{
  papplLogPrinter(printer, PAPPL_LOGLEVEL_INFO, "Printer identification triggered: Eject one label");

  pappl_device_t           *device;                      // Printer device connection
  pappl_pr_driver_data_t   driver_data;                  // Driver data
  int                      print_width;                  // Effective print width (0.1mm)
  int                      print_height;                 // Effective print height (0.1mm)
  int                      label_pitch;                  // Label pitch with gap (0.1mm)
  int                      roll_width;                   // Full roll width (0.1mm)

  // Open connection to the printer device
  device = papplPrinterOpenDevice(printer);
  if (!device)
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Failed to open device connection for printer identification");
    return;
  }

  // Get driver data to access media settings
  if (!papplPrinterGetDriverData(printer, &driver_data))
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Failed to get driver data for printer identification");
    papplPrinterCloseDevice(printer);
    return;
  }

  // Calculate dimensions from media_default (convert hundredths of mm to tenths of mm)
  print_width  = driver_data.media_default.size_width / 10;   // Effective print width
  print_height = driver_data.media_default.size_length / 10;  // Effective print height

  // Request printer IPP attributes
  ipp_t *printer_attrs = papplPrinterGetDriverAttributes(printer);
  if (!printer_attrs)
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Failed to get printer attributes");
    papplPrinterCloseDevice(printer);
    return;
  }

  // Get label dimensions using helper function
  tpcl_get_label_dimensions(printer_attrs, print_width, print_height, &label_pitch, &roll_width, NULL, printer);

  // Validate dimensions are within printer limits
  // 203dpi: pitch max 9990 (999.0mm), height max 9970 (997.0mm)
  // 300dpi: pitch max 4572 (457.2mm), height max 4552 (455.2mm)
  int max_pitch = (driver_data.y_default == 300) ? 4572 : 9990;
  int max_height = (driver_data.y_default == 300) ? 4552 : 9970;

  if (label_pitch > max_pitch)
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Label pitch %d (0.1mm) exceeds maximum %d (0.1mm) for %ddpi resolution", label_pitch, max_pitch, driver_data.y_default);
    ippDelete(printer_attrs);
    papplPrinterCloseDevice(printer);
    return;
  }

  if (print_height > max_height)
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Print height %d (0.1mm) exceeds maximum %d (0.1mm) for %ddpi resolution", print_height, max_height, driver_data.y_default);
    ippDelete(printer_attrs);
    papplPrinterCloseDevice(printer);
    return;
  }

  // Send label size command
  tpcl_cmd_label_size(device, label_pitch, print_width, print_height, roll_width, NULL, printer);

  // Build feed command parameters
  // Sensor type (0=none, 1=reflective, 2=transmissive, 3=transmissive pre-print, 4=reflective pre-print)
  const char *sensor_type = tpcl_get_str_option(printer_attrs, "sensor-type", "transmissive", NULL, printer);
  char sensor_char = tpcl_map_sensor_type(sensor_type);

  // Cut selection (0=non-cut, 1=cut)
  const char *cut_type = tpcl_get_str_option(printer_attrs, "label-cut", "non-cut", NULL, printer);
  char cut_char = tpcl_map_cut_type(cut_type);

  // Feed mode (C=batch, D=strip with backfeed sensor valid, E=strip with backfeed sensor ignored, F=partial-cut)
  const char *feed_mode = tpcl_get_str_option(printer_attrs, "feed-mode", "batch", NULL, printer);
  char feed_mode_char = tpcl_map_feed_mode(feed_mode);

  // Feed speed (retrieve from vendor option with fallback to 3)
  int print_speed = tpcl_get_int_option(printer_attrs, "print-speed", 3, NULL, printer);
  char speed_char = '0' + print_speed;
  if (print_speed > 9)
    speed_char = 'A' + (print_speed - 10);

  // Ribbon setting (0=direct thermal or thermal transfer without ribbon, 1=tt with ribbon saving, 2=tt without ribbon saving)
  char ribbon_char = '0';
  const char *media_type = driver_data.media_default.type;
  if (media_type)
  {
    if (strcmp(media_type, "thermal-transfer-ribbon-saving") == 0)
      ribbon_char = '1';
    else if (strcmp(media_type, "thermal-transfer-no-ribbon-saving") == 0)
      ribbon_char = '2';
  }

  // Send feed command
  tpcl_cmd_feed(device, sensor_char, cut_char, feed_mode_char, speed_char, ribbon_char, NULL, printer);

  papplDeviceFlush(device);
  ippDelete(printer_attrs);
  papplPrinterCloseDevice(printer);
  return;
}


/*
 * 'tpcl_print_cb()' - Print raw TPCL file callback
 *
 * Reads a file containing TPCL commands (format: application/vnd.toshiba-tpcl)
 * and sends them directly to the printer. Each command is on a separate line
 * and ends with '\n' in the format: '{...|}\n'
 * Lines starting with '#' are treated as comments and skipped.
 */

bool
tpcl_print_cb(
  pappl_job_t              *job,
  pappl_pr_options_t       *options,
  pappl_device_t           *device
)
{
  papplLogJob(job, PAPPL_LOGLEVEL_INFO, "Starting raw TPCL file printing");

  int                      fd;                           // File descriptor
  char                     line[131072];                 // Buffer for reading lines (128KB)
  ssize_t                  bytes_read;                   // Bytes read from file
  size_t                   line_pos = 0;                 // Current position in line buffer
  unsigned int             command_count = 0;            // Number of commands sent
  char                     ch;                           // Current character

  // Open the job file for reading
  fd = open(papplJobGetFilename(job), O_RDONLY);
  if (fd < 0)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Failed to open job file: %s", strerror(errno));
    return false;
  }

  // Read file character by character and process line by line
  while ((bytes_read = read(fd, &ch, 1)) > 0)
  {
    // Add character to line buffer
    if (line_pos < sizeof(line) - 1)
    {
      line[line_pos++] = ch;
    }
    else
    {
      papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Line too long (exceeds %lu bytes)", (unsigned long)sizeof(line));
      line[sizeof(line) - 1] = '\0';
      close(fd);
      return false;
    }

    // Process complete line when newline is encountered
    if (ch == '\n')
    {
      line[line_pos] = '\0';  // Null-terminate the line

      // Skip empty lines and comment lines (starting with '#')
      if (line_pos > 1 && line[0] != '#')
      {
        // Send the TPCL command to the printer
        papplDeviceWrite(device, line, line_pos);
        command_count++;

        // Log every 10th command to avoid excessive logging
        if (command_count % 10 == 0)
        {
          papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sent %u TPCL commands to printer", command_count);
        }
      }
      else if (line[0] == '#')
      {
        papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Skipping comment: %s", line);
      }

      // Reset line buffer for next line
      line_pos = 0;
    }
  }

  // Check for read errors
  if (bytes_read < 0)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Error reading job file: %s", strerror(errno));
    close(fd);
    return false;
  }

  // Process any remaining data in buffer (line without trailing newline)
  if (line_pos > 0)
  {
    line[line_pos] = '\0';

    // Skip if it's a comment
    if (line[0] != '#')
    {
      papplLogJob(job, PAPPL_LOGLEVEL_WARN, "Last line missing newline terminator, sending anyway: %s", line);
      papplDeviceWrite(device, line, line_pos);
      command_count++;
    }
  }

  // Flush device buffer to ensure all commands are sent
  papplDeviceFlush(device);

  papplLogJob(job, PAPPL_LOGLEVEL_INFO, "Raw TPCL file printing completed: %u commands sent", command_count);

  close(fd);
  return true;
}


/*
 * 'tpcl_rstartjob_cb()' - Start a print job
 *
 * Creates job data structure and sends job initialization commands:
 *   1. {Daaaa,bbbb,cccc,dddd|} -> Label size definition
 *   2. (if not zero) {AX;abbb,cddd,eff|} -> Position fine adjustment
 *   3. (if not zero) {AY;abb,c|} -> Print density fine adjustment
 *   4. (if labe size changed) {Tabcde|} -> Feed label
 * 
 * Note: We assume, that all pages of a job are of the same length!
 */

bool
tpcl_rstartjob_cb(
  pappl_job_t              *job,
  pappl_pr_options_t       *options,
  pappl_device_t           *device
)
{
  papplLogJob(job, PAPPL_LOGLEVEL_INFO, "Starting TPCL print job");

  // Get printer handle for IPP attribute access
  pappl_printer_t *printer = papplJobGetPrinter(job);
  if (!printer)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Failed to get printer handle");
    return false;
  }

  // Get printer IPP attributes for vendor options
  ipp_t *printer_attrs = papplPrinterGetDriverAttributes(printer);
  if (!printer_attrs)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Failed to get printer attributes");
    return false;
  }

  // Allocate and set the per-job driver data pointer
  tpcl_job_t *tpcl_job;
  tpcl_job = (tpcl_job_t *)calloc(1, sizeof(tpcl_job_t));
  if (!tpcl_job)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Failed to allocate job data structure");
    ippDelete(printer_attrs);
    return false;
  }
  papplJobSetData(job, tpcl_job);

  // Set graphics mode from vendor options
  const char *graphics_mode = tpcl_get_str_option(printer_attrs, "graphics-mode", "topix", job, NULL);

  if (strcmp(graphics_mode, "nibble-and") == 0)
    tpcl_job->gmode = TEC_GMODE_NIBBLE_AND;
  else if (strcmp(graphics_mode, "hex-and") == 0)
    tpcl_job->gmode = TEC_GMODE_HEX_AND;
  else if (strcmp(graphics_mode, "topix") == 0)
    tpcl_job->gmode = TEC_GMODE_TOPIX;
  else if (strcmp(graphics_mode, "nibble-or") == 0)
    tpcl_job->gmode = TEC_GMODE_NIBBLE_OR;
  else if (strcmp(graphics_mode, "hex-or") == 0)
    tpcl_job->gmode = TEC_GMODE_HEX_OR;
  else
  {
    papplLogJob(job, PAPPL_LOGLEVEL_WARN, "Unknown graphics mode '%s', defaulting to TOPIX", graphics_mode);
    tpcl_job->gmode = TEC_GMODE_TOPIX;
  }
  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Graphics mode set to: %s (%d)", graphics_mode, tpcl_job->gmode);

  if (tpcl_job->gmode == TEC_GMODE_TOPIX)
  {
    // Check if resolution is 150 or 300, as TOPIX mode does not work with other resolutions
    if ((options->header.HWResolution[0] != 150 && options->header.HWResolution[0] != 300) ||
        (options->header.HWResolution[1] != 150 && options->header.HWResolution[1] != 300) ||
        (options->header.HWResolution[0] != options->header.HWResolution[1]))
    {
      papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "TOPIX mode only supports 150x150 or 300x300 dpi resolution. Requested: %ux%u dpi", options->header.HWResolution[0], options->header.HWResolution[1]);
      tpcl_free_job_buffers(job, tpcl_job);
      return false;
    }

    // Allocate buffers for TOPIX compression
    tpcl_job->buffer  = malloc(options->header.cupsBytesPerLine);
    tpcl_job->compbuf = tpcl_compbuf_create(options->header.cupsBytesPerLine, job);

    if (!tpcl_job->buffer || !tpcl_job->compbuf)
    {
      papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Failed to allocate TOPIX buffers");
      tpcl_free_job_buffers(job, tpcl_job);
      ippDelete(printer_attrs);
      return false;
    }
  }
  else
  {
    // Allocate buffer for hex or nibble modes
    tpcl_job->buffer = malloc(options->header.cupsBytesPerLine);

    if (!tpcl_job->buffer)
    {
      papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Failed to allocate line buffer for HEX / Nibble mode");
      tpcl_free_job_buffers(job, tpcl_job);
      ippDelete(printer_attrs);
      return false;
    }
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "HEX mode buffer allocated: line=%u bytes", options->header.cupsBytesPerLine);
  }

  // Calculate label dimensions from page size
  // cupsPageSize is in points (1/72 inch), convert to 0.1mm: points * 25.4 * 10 / 72
  // Add 0.5 before truncating to properly round instead of truncating (avoids rounding errors)
  tpcl_job->print_width  = (int)(options->header.cupsPageSize[0] * MM_PER_INCH * 10.0 / POINTS_PER_INCH + 0.5);  // Effective print width (0.1mm)
  tpcl_job->print_height = (int)(options->header.cupsPageSize[1] * MM_PER_INCH * 10.0 / POINTS_PER_INCH + 0.5);  // Effective print height (0.1mm)

  // Get label gap and roll margin from printer settings
  int label_gap = tpcl_get_int_option(printer_attrs, "label-gap", 50, job, NULL);
  int roll_margin = tpcl_get_int_option(printer_attrs, "roll-margin", 10, job, NULL);

  // Calculate label pitch and roll width from retrieved values
  tpcl_job->label_pitch = tpcl_job->print_height + label_gap;
  tpcl_job->roll_width = tpcl_job->print_width + roll_margin;

  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Calculated label dimensions from page size: width=%d (0.1mm), height=%d (0.1mm), pitch=%d (0.1mm), roll=%d (0.1mm)", tpcl_job->print_width, tpcl_job->print_height, tpcl_job->label_pitch, tpcl_job->roll_width);
  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Maximum image resolution at %ux%udpi: width=%u dots, height=%u dots", options->header.HWResolution[0], options->header.HWResolution[1], (unsigned int) (options->header.HWResolution[0] * options->header.cupsPageSize[0] / POINTS_PER_INCH), (unsigned int) (options->header.HWResolution[1] * options->header.cupsPageSize[1] / POINTS_PER_INCH));

  // Calculate buffer length in bytes as sent to printer
  if (options->header.cupsBitsPerPixel == 0)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Invalid raster data: cupsBitsPerPixel is 0");
    tpcl_free_job_buffers(job, tpcl_job);
    ippDelete(printer_attrs);
    return false;
  }

  // For 8-bit grayscale input, after dithering we have 1 bit per pixel
  // For 1-bit input, it's already packed
  // In both cases, calculate the packed size: ceil(width / 8)
  tpcl_job->buffer_len = (options->header.cupsWidth + 7) / 8;
  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Calculated buffer_len=%zu bytes (for %u pixels)", tpcl_job->buffer_len, options->header.cupsWidth);

  // Validate dimensions are within printer limits before sending label size command
  // 203dpi: pitch max 9990 (999.0mm), height max 9970 (997.0mm)
  // 300dpi: pitch max 4572 (457.2mm), height max 4552 (455.2mm)
  int max_pitch = (options->header.HWResolution[1] == 300) ? 4572 : 9990;
  int max_height = (options->header.HWResolution[1] == 300) ? 4552 : 9970;

  if (tpcl_job->label_pitch > max_pitch)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Label pitch %d (0.1mm) exceeds maximum %d (0.1mm) for %udpi resolution", tpcl_job->label_pitch, max_pitch, options->header.HWResolution[1]);
    tpcl_free_job_buffers(job, tpcl_job);
    ippDelete(printer_attrs);
    return false;
  }

  if (tpcl_job->print_height > max_height)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Print height %d (0.1mm) exceeds maximum %d (0.1mm) for %udpi resolution", tpcl_job->print_height, max_height, options->header.HWResolution[1]);
    tpcl_free_job_buffers(job, tpcl_job);
    ippDelete(printer_attrs);
    return false;
  }

  // Send label size command
  tpcl_cmd_label_size(device, tpcl_job->label_pitch, tpcl_job->print_width, tpcl_job->print_height, tpcl_job->roll_width, job, NULL);

  // Send feed adjustment command - only send when necessary (when any value != 0)
  // Get feed adjustment values from printer settings using helper function
  int feed_adjustment, cut_position_adjustment, backfeed_adjustment;
  bool has_adjustments = tpcl_get_feed_adjustments(printer_attrs, &feed_adjustment, &cut_position_adjustment, &backfeed_adjustment, job, NULL);

  // Only send AX command if at least one adjustment value is non-zero
  if (has_adjustments)
  {
    // Feed: negative = forward (-), positive = backward (+)
    // Cut position: negative = forward (-), positive = backward (+)
    // Backfeed: negative = decrease (-), positive = increase (+)
    tpcl_cmd_position_adjust(device, feed_adjustment, cut_position_adjustment, backfeed_adjustment, job, NULL);
  }
  else
  {
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Skipping AX command - all adjustment values are 0");
  }

  // Print density adjustment command - only send when print-darkness is not 0
  // Get print darkness from printer settings (-10 to 10)
  int print_darkness = tpcl_get_int_option(printer_attrs, "print-darkness", 0, job, NULL);

  // Only send AY command if darkness adjustment is non-zero
  if (print_darkness != 0)
  {
    // Get driver data to determine media type (thermal transfer or direct thermal)
    pappl_pr_driver_data_t driver_data;
    if (!papplPrinterGetDriverData(printer, &driver_data))
    {
      papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Failed to get driver data for AY command");
      tpcl_free_job_buffers(job, tpcl_job);
      ippDelete(printer_attrs);
      return false;
    }
    else
    {
      // Determine if using thermal transfer (0) or direct thermal (1)
      // thermal-transfer* types use 0, direct-thermal uses 1
      char mode_char = '1';  // Default to direct thermal
      const char *media_type = driver_data.media_default.type;
      if (media_type)
      {
        if (strncmp(media_type, "thermal-transfer", 16) == 0)
          mode_char = '0';
        papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Media type: %s, AY mode: %c", media_type, mode_char);
      }

      tpcl_cmd_darkness_adjust(device, print_darkness, mode_char, job, NULL);
    }
  }
  else
  {
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Skipping AY command - print darkness is 0");
  }

  // Check if label dimensions have changed and update state file
  bool label_size_changed = tpcl_state_check_and_update(printer, tpcl_job->print_width, tpcl_job->print_height, label_gap, roll_margin, job);

  // If label size changed and feed-on-label-size-change is enabled, send feed command
  const char *feed_on_change_str = tpcl_get_str_option(printer_attrs, "feed-on-label-size-change", "no", job, NULL);
  bool should_feed = label_size_changed && (strcmp(feed_on_change_str, "yes") == 0);

  if (should_feed)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Label size changed and feed-on-label-size-change is enabled, sending feed command");

    // Build feed command dynamically {Tabcde|} using same logic as tpcl_identify_cb
    pappl_pr_driver_data_t driver_data;
    if (!papplPrinterGetDriverData(printer, &driver_data))
    {
      papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Failed to get driver data for feed command");
    }
    else
    {
      // a: Sensor type (0=none, 1=reflective, 2=transmissive, 3=transmissive pre-print, 4=reflective pre-print)
      char sensor_char = '2';
      const char *sensor_type = tpcl_get_str_option(printer_attrs, "sensor-type", "transmissive", job, NULL);
      sensor_char = tpcl_map_sensor_type(sensor_type);

      // b: Cut selection (0=non-cut, 1=cut)
      const char *cut_type = tpcl_get_str_option(printer_attrs, "label-cut", "non-cut", job, NULL);
      char cut_char = tpcl_map_cut_type(cut_type);

      // c: Feed mode (C=batch, D=strip with backfeed sensor valid, E=strip with backfeed sensor ignored, F=partial-cut)
      const char *feed_mode = tpcl_get_str_option(printer_attrs, "feed-mode", "batch", job, NULL);
      char feed_mode_char = tpcl_map_feed_mode(feed_mode);

      // d: Feed speed (retrieve from vendor option with fallback to 3)
      int print_speed = tpcl_get_int_option(printer_attrs, "print-speed", 3, job, NULL);
      char speed_char = '0' + print_speed;
      if (print_speed > 9)
        speed_char = 'A' + (print_speed - 10);

      // e: Ribbon setting (0=direct thermal or thermal transfer without ribbon, 1=tt with ribbon saving, 2=tt without ribbon saving)
      char ribbon_char = '0';
      const char *media_type = driver_data.media_default.type;
      if (media_type)
      {
        if (strcmp(media_type, "thermal-transfer-ribbon-saving") == 0)
          ribbon_char = '1';
        else if (strcmp(media_type, "thermal-transfer-no-ribbon-saving") == 0)
          ribbon_char = '2';
      }

      tpcl_cmd_feed(device, sensor_char, cut_char, feed_mode_char, speed_char, ribbon_char, job, NULL);
    }
  }

  ippDelete(printer_attrs);
  return true;
}


/*
 * 'tpcl_rstartpage_cb()' - Start a page
 *
 * Sends page initialization commands:
 *   1. {C|} -> Clear image buffer
 *   2. (if not TOPIX compression) {SG;aaaa,bbbbb,cccc,ddddd,e,... -> Image headers
 */

bool
tpcl_rstartpage_cb(
  pappl_job_t              *job,
  pappl_pr_options_t       *options,
  pappl_device_t           *device,
  unsigned                 page
)
{
  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Starting page %u", page);

  // Fetch the job driver data pointer
  tpcl_job_t *tpcl_job = (tpcl_job_t *)papplJobGetData(job);
  if (!tpcl_job)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Job data structure not initialized");
    return false;
  }

  // Clear image buffer command
  tpcl_cmd_clear_buffer(device, job, NULL);

  if (tpcl_job->gmode == TEC_GMODE_TOPIX)
  {
    // Zero buffers in case of TOPIX compression
    tpcl_compbuf_reset(tpcl_job->compbuf);
    tpcl_job->y_offset = 0;

    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "TOPIX buffers reset for new page");
  }
  else
  {
    // For hex and nibble mode, send the SG command header
    tpcl_cmd_graphics_header(device, 0, 0, options->header.cupsWidth, options->header.cupsHeight, tpcl_job->gmode, job, NULL);
  }
  return true;
}


/*
 * 'tpcl_rwriteline_cb()' - Write a line of raster data
 *
 * Dithers, inverts and compresses each line of data, then sends it to the device.
 * 
 * With TOPIX compression we need automatic buffer flushing, so command order is:
 *   1. {SG;aaaa,bbbbb,cccc,ddddd,e,... -> Image headers (start and flush)
 *   2. ...ggg---ggg... -> Compressed image body (always)
 *   3. ...|} -> Command footer (flush and end)
 * If the image is larger than the available buffer (TOPIX has a upper limit of 0xFFFF (approx. 65 kb)
 * buffer size due to indexing), end the command, send it and start a new command with updated
 * y-coordinates.
 * 
 * In hex and nibble modes, life is simpler:
 *   1. ...ggg---ggg...  -> Image body
 */

bool
tpcl_rwriteline_cb(
  pappl_job_t              *job,
  pappl_pr_options_t       *options,
  pappl_device_t           *device,
  unsigned                 y,
  const unsigned char      *line
)
{
  if (y == 0)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Starting line %u (logging debug messages for the first and last 3 lines only)", y);
  }

  // Fetch the job driver data pointer
  tpcl_job_t *tpcl_job = (tpcl_job_t *)papplJobGetData(job);
  if (!tpcl_job || !tpcl_job->buffer)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Line %u: Job data or buffer not initialized", y);
    return false;
  }

  // For 8bit color depth (1 byte = 1 pixel, grayscale), dither and compact to 1bit depth
  if (options->header.cupsBitsPerPixel == 8)
  {
    if ((y < 3) | (y > (options->header.cupsHeight - 4)))
    {
      papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Line %u: Using 8 bit to 1 bit dithering for image output", y);
    }

    // Clear output buffer
    memset(tpcl_job->buffer, 0, tpcl_job->buffer_len);

    // Dither and pack to 8 pixels per output byte, MSB-first. PAPPL auto-selects the appropriate dither array.
    for (unsigned int x = 0; x < options->header.cupsBytesPerLine; x++)
    {
      if (line[x] >= options->dither[y & 15][x & 15])    // If pixel is above threshold, set bit to 1
      {
        tpcl_job->buffer[x / 8] |= (0x80 >> (x & 7));    // Set bit MSB-first
      }
    }
  }
  // For 1bit color depth (1 byte = 8 pixels, black and white), just copy to target buffer
  else if (options->header.cupsBitsPerPixel == 1)
  {
    if ((y < 3) | (y > (options->header.cupsHeight - 4)))
    {
      papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Line %u: Using native 1 bit color depth for image output", y);
    }

    // Move data to target buffer
    memcpy(tpcl_job->buffer, line, options->header.cupsBytesPerLine);
  }
  else
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Line %u: Only 1 bit or 8 bit color depths are supported, request was for %u bit", y, options->header.cupsBitsPerPixel);
    tpcl_free_job_buffers(job, tpcl_job);
    return false;
  }

  // Determine if the print job is black ink plane (1 = black) or white ink plane (1 = white). Printer expects black ink plane (1 = black)
  if (options->header.cupsColorSpace == CUPS_CSPACE_SW)  // 1 = white -> invert all bits
  {
    if ((y < 3) | (y > (options->header.cupsHeight - 4)))
    {
      papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Line %u: Flipping bits to translate from white ink plane (1 = white) to black ink plane (1 = black)", y);
    }

    for (unsigned int x = 0; x < tpcl_job->buffer_len; x++)
    {
      tpcl_job->buffer[x] = (unsigned char)~tpcl_job->buffer[x];
    }
  } 
  else if (options->header.cupsColorSpace != CUPS_CSPACE_K)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Line %u: Only K(3) and SW(18) color spaces supported, request was for space (%d)", y, options->header.cupsColorSpace);
    tpcl_free_job_buffers(job, tpcl_job);
    return false;
  }

  // Determine the transmission mode
  if ((tpcl_job->gmode == TEC_GMODE_HEX_AND) | (tpcl_job->gmode == TEC_GMODE_HEX_OR))
  {
    if ((y < 3) | (y > (options->header.cupsHeight - 4)))
    {
      papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Line %u: Transmitting %lu bytes in hex mode", y, (unsigned long)tpcl_job->buffer_len);
    }

    papplDeviceWrite(device, tpcl_job->buffer, tpcl_job->buffer_len);
  }
  else if ((tpcl_job->gmode == TEC_GMODE_NIBBLE_AND) | (tpcl_job->gmode == TEC_GMODE_NIBBLE_OR))
  {
    // Mode to transmit data encoded as ASCII characters '0' (0x30, 0b0011 0000) to '?' (0x3F, 0b0011 1111)
    // Split incoming buffer into high and low nibble, prefix 0b0011 high nibble for both bytes and send

    if ((y < 3) | (y > (options->header.cupsHeight - 4)))
    {
      papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Line %u: Transmitting %lu bytes in nibble mode (ASCII mode)", y, (unsigned long)(tpcl_job->buffer_len * 2));
    }

    // Debug output: allocate buffer for ASCII representation file dump
    char *debug_buffer = NULL;
    if (papplSystemGetLogLevel(papplPrinterGetSystem(papplJobGetPrinter(job))) >= PAPPL_LOGLEVEL_DEBUG)
    {
      unsigned int line_chars = tpcl_job->buffer_len * 2;
      debug_buffer = malloc(line_chars + 1);
      if (debug_buffer)
      {
        memset(debug_buffer, 0, line_chars + 1);
      }
    }

    for (unsigned int x = 0; x < tpcl_job->buffer_len; x++)
    {
      // Split into two ASCII bytes: 0x30 | nibble
      unsigned char out[2] = {
        (unsigned char)(0x30 | ((tpcl_job->buffer[x] >> 4) & 0x0F)), // high nibble to low nibble
        (unsigned char)(0x30 | ( tpcl_job->buffer[x]       & 0x0F))  // low nibble to low nibble
      };

      // Store ASCII representation in debug buffer
      if (debug_buffer)
      {
        debug_buffer[x * 2]     = (out[0] >= 0x30 && out[0] <= 0x3F) ? out[0] : '.';
        debug_buffer[x * 2 + 1] = (out[1] >= 0x30 && out[1] <= 0x3F) ? out[1] : '.';
      }

      papplDeviceWrite(device, out, 2);
    }

    // Dump debug output to file and free buffer
    if (debug_buffer)
    {
      char filename[256];
      snprintf(filename, sizeof(filename), "/tmp/rastertotpcl-nibble-dump-%d.out", getpid());

      FILE *fp = fopen(filename, "a");
      if (fp)
      {
        // Write header for line 0
        if (y == 0)
        {
          fprintf(fp, "\n### Job %d, Page %u ###\n", papplJobGetID(job), options->header.cupsInteger[0]);
        }

        fprintf(fp, "Line %u: %s\n", y, debug_buffer);
        fclose(fp);

        // Print info message at last line
        if (y == options->header.cupsHeight - 1)
        {
          papplLogJob(job, PAPPL_LOGLEVEL_INFO, "Dump file with image data written to %s", filename);
        }
      }
      else
      {
        papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Failed to open debug dump file: %s", filename);
      }

      free(debug_buffer);
    }
  }
  else if (tpcl_job->gmode == TEC_GMODE_TOPIX)
  {
    // TOPIX compression mode. Always compress line into compression buffer and check if buffer is close to full.
    // If buffer is close to full, send data to printer, increment y-offset and zero buffers to start a new run

    if ((y < 3) | (y > (options->header.cupsHeight - 4)))
    {
      papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Line %u: Compressing %lu bytes in TOPIX mode", y, (unsigned long)tpcl_job->buffer_len);
    }

    // Compress line using TOPIX algorithm
    tpcl_topix_compress_line(tpcl_job->compbuf, tpcl_job->buffer);

    // Check if compression buffer is getting full, flush if needed. Also flush if this is the last line
    size_t buffer_used      = tpcl_topix_get_buffer_used(tpcl_job->compbuf);
    size_t buffer_threshold = TPCL_COMP_BUFFER_MAX - (tpcl_job->buffer_len + ((tpcl_job->buffer_len / 8) * 3));

    if ((buffer_used > buffer_threshold) | (y == (options->header.cupsHeight - 1)))
    {
      papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Line %u: TOPIX buffer full (%lu/65535 bytes) or last line, flushing. Y offset for this image: %d (0.1mm)", y, (unsigned long)buffer_used, tpcl_job->y_offset);

      ssize_t bytes_written = tpcl_topix_flush(tpcl_job->compbuf, device, tpcl_job->y_offset, options->header.cupsWidth, options->header.HWResolution[0], tpcl_job->gmode);

      // Y offset for next image in 0.1mm (for TOPIX)
      tpcl_job->y_offset = (int) (y + 1) * MM_PER_INCH * 10.0 / options->header.HWResolution[0];

      papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Line %u: TOPIX buffer flushed, %ld bytes sent. Y offset for next image: %d (0.1mm)", y, (long)bytes_written, tpcl_job->y_offset);
    }
  }
  else
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Line %u: Graphics transmission mode %d not supported", y, tpcl_job->gmode);
    tpcl_free_job_buffers(job, tpcl_job);
    return false;
  }

  return true;
}


/*
 * 'tpcl_rendpage_cb()' - End a page
 *
 * Sends page finalization commands:
 *   1. (if not TOPIX compression) ...|} -> Command footer
 *   2. {XS;I,aaaa,bbbcdefgh|} - Execute print command
 *   3. (if on a TCP connection) TCP workaround: 1024 spaces padding
 */

bool
tpcl_rendpage_cb(
  pappl_job_t              *job,
  pappl_pr_options_t       *options,
  pappl_device_t           *device,
  unsigned                 page
)
{
  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Ending page %u", page);

  // Fetch the job driver data pointer
  tpcl_job_t *tpcl_job = (tpcl_job_t *)papplJobGetData(job);
  if (!tpcl_job)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Job data structure not initialized");
    return false;
  }

  // Close hex/nibble graphics
  if (tpcl_job->gmode != TEC_GMODE_TOPIX)
  {
    // Close HEX mode SG command
    papplDevicePuts(device, "|}\n");
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Closing HEX graphics data with: |}}");
  }

  // Get printer handle for IPP attribute access
  pappl_printer_t *printer = papplJobGetPrinter(job);
  if (!printer)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Failed to get printer handle");
    tpcl_free_job_buffers(job, tpcl_job);
    return false;
  }

  // Get printer IPP attributes for vendor options
  ipp_t *printer_attrs = papplPrinterGetDriverAttributes(printer);
  if (!printer_attrs)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Failed to get printer attributes");
    tpcl_free_job_buffers(job, tpcl_job);
    return false;
  }

  // Get driver data for media type and speed
  pappl_pr_driver_data_t driver_data;
  if (!papplPrinterGetDriverData(printer, &driver_data))
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Failed to get driver data for XS command");
    ippDelete(printer_attrs);
    tpcl_free_job_buffers(job, tpcl_job);
    return false;
  }

  // Build XS (issue label) command dynamically
  // Format: {XS;I,aaaa,bbbcdefgh|}\n

  // aaaa: Number of labels to be issued (0001 to 9999) - get from job copies
  int num_copies = papplJobGetCopies(job);
  if (num_copies < 1 || num_copies > 9999)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Invalid number of copies %d, must be in range [1-9999]", num_copies);
    ippDelete(printer_attrs);
    tpcl_free_job_buffers(job, tpcl_job);
    return false;
  }

  // bbb: Cut interval (000 to 100, 000 = no cut)
  int cut_interval = tpcl_get_int_option(printer_attrs, "cut-interval", 0, job, NULL);
  if (cut_interval < 0 || cut_interval > 100)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Invalid cut interval %d, must be in range [0-100]", cut_interval);
    ippDelete(printer_attrs);
    tpcl_free_job_buffers(job, tpcl_job);
    return false;
  }

  // c: Type of sensor (0=none, 1=reflective, 2=transmissive, 3=transmissive pre-print, 4=reflective pre-print)
  const char *sensor_type = tpcl_get_str_option(printer_attrs, "sensor-type", "transmissive", job, NULL);
  char sensor_char = tpcl_map_sensor_type(sensor_type);

  // d: Issue mode (C=batch, D=strip with backfeed sensor valid, E=strip with backfeed sensor ignored, F=partial-cut)
  const char *feed_mode = tpcl_get_str_option(printer_attrs, "feed-mode", "batch", job, NULL);
  char feed_mode_char = tpcl_map_feed_mode(feed_mode);

  // e: Issue speed (retrieve from vendor option with fallback to 3)
  int print_speed = tpcl_get_int_option(printer_attrs, "print-speed", 3, job, NULL);
  char speed_char = '0' + print_speed;
  if (print_speed > 9)
    speed_char = 'A' + (print_speed - 10);

  // f: With/without ribbon (0=direct thermal or thermal transfer without ribbon, 1=tt with ribbon saving, 2=tt without ribbon saving)
  char ribbon_char = '0';
  const char *media_type = driver_data.media_default.type;
  if (media_type)
  {
    if (strcmp(media_type, "thermal-transfer-ribbon-saving") == 0)
      ribbon_char = '1';
    else if (strcmp(media_type, "thermal-transfer-no-ribbon-saving") == 0)
      ribbon_char = '2';
  }

  // g: Tag rotation (0 = no rotation, PAPPL handles rotation)
  char rotation_char = '0';

  // h: Type of status response (0 = not needed)
  char status_response_char = '0';

  // Send the XS command
  tpcl_cmd_issue_label(device, num_copies, cut_interval, sensor_char, feed_mode_char, speed_char, ribbon_char, rotation_char, status_response_char, job, NULL);

  // Workaround: Send padding to avoid TCP zero-window error on network connections
  const char *device_uri = papplPrinterGetDeviceURI(printer);
  if (device_uri && strncmp(device_uri, "socket://", 9) == 0)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending 1024 space padding (TCP workaround for network connection)");
    papplDevicePrintf(device, "%1024s", "");
  }

  ippDelete(printer_attrs);
  papplDeviceFlush(device);
  return true;
}


/*
 * 'tpcl_rendjob_cb()' - End a job
 *
 * No specific commands sent at end of job, just cleanup job data structure.
 * One exception:
 *   1. (if device is a BEV4T) Workaround: Send 600 null bytes as dummy data  
 */

bool
tpcl_rendjob_cb(
  pappl_job_t              *job,
  pappl_pr_options_t       *options,
  pappl_device_t           *device
)
{
  papplLogJob(job, PAPPL_LOGLEVEL_INFO, "Ending TPCL print job");

  tpcl_job_t *tpcl_job = (tpcl_job_t *)papplJobGetData(job);
  if (!tpcl_job)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Job data structure not initialized");
    return false;
  }

  // Workaround: Send dummy data to avoid last packet lost bug on B-EV4T models
  pappl_printer_t *printer = papplJobGetPrinter(job);
  if (printer)
  {
    const char *driver_name = papplPrinterGetDriverName(printer);
    if (driver_name && strstr(driver_name, "B-EV4T"))
    {
      static unsigned char dummy_data[600] = {0};  // 600 null bytes for BEV4T workaround
      papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending 600 null bytes (BEV4T workaround)");
      papplDeviceWrite(device, dummy_data, sizeof(dummy_data));
      papplDeviceFlush(device);
    }
  }

  // Free buffers
  tpcl_free_job_buffers(job, tpcl_job);
  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Freeing page buffers and job data structure");

  return true;
}


/*
 * 'tpcl_testpage_cb()' - Print test file callback
 *
 * Generates TPCL commands to print a test page:
 *   1. D  - Set label size
 *   2. AX - Feed adjustment (only if values are non-zero)
 *   3. AY - Print density (only if darkness is non-zero)
 *   4. T  - Feed paper (only if label size changed)
 *   5. C  - Clear buffer
 *   6. LC - Line format command 
 *   7. XS - Issue label
 */

const char* tpcl_testpage_cb(
  pappl_printer_t          *printer,
  char                     *buffer,
  size_t                   bufsize
)
{
  pappl_device_t           *device;                      // Printer device connection
  pappl_pr_driver_data_t   driver_data;                  // Driver data
  int                      print_width;                  // Effective print width (0.1mm)
  int                      print_height;                 // Effective print height (0.1mm)
  int                      label_pitch;                  // Label pitch with gap (0.1mm)
  int                      roll_width;                   // Full roll width (0.1mm)

  papplLogPrinter(printer, PAPPL_LOGLEVEL_INFO, "Printing test page");

  // Open connection to the printer device
  device = papplPrinterOpenDevice(printer);
  if (!device)
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Failed to open device connection for test page");
    return NULL;
  }

  // Get driver data to access media settings
  if (!papplPrinterGetDriverData(printer, &driver_data))
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Failed to get driver data");
    papplPrinterCloseDevice(printer);
    return NULL;
  }

  // Calculate dimensions from media_default (convert hundredths of mm to tenths of mm)
  print_width  = driver_data.media_default.size_width / 10;   // Effective print width
  print_height = driver_data.media_default.size_length / 10;  // Effective print height

  // Get printer IPP attributes for vendor options
  ipp_t *printer_attrs = papplPrinterGetDriverAttributes(printer);
  if (!printer_attrs)
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Failed to get printer attributes");
    papplPrinterCloseDevice(printer);
    return NULL;
  }

  // Get label dimensions using helper function
  tpcl_get_label_dimensions(printer_attrs, print_width, print_height, &label_pitch, &roll_width, NULL, printer);

  // Send label size command
  tpcl_cmd_label_size(device, label_pitch, print_width, print_height, roll_width, NULL, printer);

  // 2. AX command - Feed adjustment (only if values are non-zero)
  int feed_adjustment, cut_position_adjustment, backfeed_adjustment;
  bool has_adjustments = tpcl_get_feed_adjustments(printer_attrs, &feed_adjustment, &cut_position_adjustment, &backfeed_adjustment, NULL, printer);

  // Only send AX command if at least one adjustment value is non-zero
  if (has_adjustments)
  {
    tpcl_cmd_position_adjust(device, feed_adjustment, cut_position_adjustment, backfeed_adjustment, NULL, printer);
  }
  else
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Skipping AX command - all adjustment values are 0");
  }

  // 3. AY command - Print density (only if darkness is non-zero)
  int print_darkness = tpcl_get_int_option(printer_attrs, "print-darkness", 0, NULL, printer);

  // Only send AY command if darkness adjustment is non-zero
  if (print_darkness != 0)
  {
    // Determine if using thermal transfer (0) or direct thermal (1)
    char mode_char = '1';  // Default to direct thermal
    const char *media_type = driver_data.media_default.type;
    if (media_type)
    {
      if (strncmp(media_type, "thermal-transfer", 16) == 0)
        mode_char = '0';
      papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Media type: %s, AY mode: %c", media_type, mode_char);
    }

    tpcl_cmd_darkness_adjust(device, print_darkness, mode_char, NULL, printer);
  }
  else
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Skipping AY command - print darkness is 0");
  }

  // 4. T command - Feed paper (only if label size changed from previous state)
  bool label_size_changed = tpcl_state_check_and_update(printer, print_width, print_height, label_pitch - print_height, roll_width - print_width, NULL);

  // Check if feed-on-label-size-change is enabled
  const char *feed_on_change_str = tpcl_get_str_option(printer_attrs, "feed-on-label-size-change", "no", NULL, printer);
  bool should_feed = label_size_changed && (strcmp(feed_on_change_str, "yes") == 0);

  if (should_feed)
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Label size changed and feed-on-label-size-change is enabled, sending feed command");

    // a: Sensor type (0=none, 1=reflective, 2=transmissive, 3=transmissive pre-print, 4=reflective pre-print)
    const char *sensor_type = tpcl_get_str_option(printer_attrs, "sensor-type", "transmissive", NULL, printer);
    char sensor_char = tpcl_map_sensor_type(sensor_type);

    // b: Cut selection (0=non-cut, 1=cut)
    const char *cut_type = tpcl_get_str_option(printer_attrs, "label-cut", "non-cut", NULL, printer);
    char cut_char = tpcl_map_cut_type(cut_type);

    // c: Feed mode (C=batch, D=strip with backfeed sensor valid, E=strip with backfeed sensor ignored, F=partial-cut)
    const char *feed_mode = tpcl_get_str_option(printer_attrs, "feed-mode", "batch", NULL, printer);
    char feed_mode_char = tpcl_map_feed_mode(feed_mode);

    // d: Feed speed (retrieve from vendor option with fallback to 3)
    int print_speed = tpcl_get_int_option(printer_attrs, "print-speed", 3, NULL, printer);
    char speed_char = '0' + print_speed;
    if (print_speed > 9)
      speed_char = 'A' + (print_speed - 10);

    // e: Ribbon setting (0=direct thermal or thermal transfer without ribbon, 1=tt with ribbon saving, 2=tt without ribbon saving)
    char ribbon_char = '0';
    const char *media_type = driver_data.media_default.type;
    if (media_type)
    {
      if (strcmp(media_type, "thermal-transfer-ribbon-saving") == 0)
        ribbon_char = '1';
      else if (strcmp(media_type, "thermal-transfer-no-ribbon-saving") == 0)
        ribbon_char = '2';
    }

    tpcl_cmd_feed(device, sensor_char, cut_char, feed_mode_char, speed_char, ribbon_char, NULL, printer);
  }

  // Send clear image buffer command
  tpcl_cmd_clear_buffer(device, NULL, printer);

  // 6. LC command - Line format command - draw concentric boxes
  int box_spacing = 45;                                  // Spacing between boxes in 0.1mm
  int min_dimension = 50;                                // Minimum Box dimension in 0.1mm

  // Calculate line width in dots based on resolution
  // Assuming 203dpi: 0.5mm = 0.5 / 25.4 * 203 ≈ 4 dots
  // Assuming 300dpi: 0.5mm = 0.5 / 25.4 * 300 ≈ 6 dots
  int line_width_dots = (driver_data.y_default == 300) ? 6 : 4;

  papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Drawing concentric boxes: spacing=%d, line_width_dots=%d", box_spacing, line_width_dots);

  // Draw boxes from largest (full label) to smallest
  int box_num = 0;
  for (int offset = 0; ; offset += box_spacing, box_num++)
  {
    // Calculate box dimensions
    int x1 = offset;
    int y1 = offset;
    int x2 = print_width - offset;
    int y2 = print_height - offset;

    // Calculate box size
    int box_width = x2 - x1;
    int box_height = y2 - y1;

    // Stop if box would be too small
    if (box_width < min_dimension || box_height < min_dimension)
    {
      papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Stopping box drawing: box %d would be %dx%d (min=%d)",
        box_num, box_width, box_height, min_dimension);
      break;
    }

    // Draw the box using LC command
    // e=1 for rectangle, f=line width in dots
    tpcl_cmd_line(device, x1, y1, x2, y2, 1, line_width_dots, NULL, printer);
  }

  // 7. XS command - Issue label
  int num_copies = 1;

  // bbb: Cut interval (000 to 100, 000 = no cut)
  int cut_interval = tpcl_get_int_option(printer_attrs, "cut-interval", 0, NULL, printer);

  // c: Type of sensor (0=none, 1=reflective, 2=transmissive, 3=transmissive pre-print, 4=reflective pre-print)
  const char *sensor_type = tpcl_get_str_option(printer_attrs, "sensor-type", "transmissive", NULL, printer);
  char sensor_char = tpcl_map_sensor_type(sensor_type);

  // d: Issue mode (C=batch, D=strip with backfeed sensor valid, E=strip with backfeed sensor ignored, F=partial-cut)
  const char *feed_mode = tpcl_get_str_option(printer_attrs, "feed-mode", "batch", NULL, printer);
  char feed_mode_char = tpcl_map_feed_mode(feed_mode);

  // e: Issue speed (retrieve from vendor option with fallback to 3)
  int print_speed = tpcl_get_int_option(printer_attrs, "print-speed", 3, NULL, printer);
  char speed_char = '0' + print_speed;
  if (print_speed > 9)
    speed_char = 'A' + (print_speed - 10);

  // f: With/without ribbon (0=direct thermal or thermal transfer without ribbon, 1=tt with ribbon saving, 2=tt without ribbon saving)
  char ribbon_char = '0';
  const char *media_type = driver_data.media_default.type;
  if (media_type)
  {
    if (strcmp(media_type, "thermal-transfer-ribbon-saving") == 0)
      ribbon_char = '1';
    else if (strcmp(media_type, "thermal-transfer-no-ribbon-saving") == 0)
      ribbon_char = '2';
  }

  // g: Tag rotation (0 = no rotation)
  char rotation_char = '0';

  // h: Type of status response (0 = not needed)
  char status_response_char = '0';

  // Send XS command
  tpcl_cmd_issue_label(device, num_copies, cut_interval, sensor_char, feed_mode_char, speed_char, ribbon_char, rotation_char, status_response_char, NULL, printer);

  papplDeviceFlush(device);

  ippDelete(printer_attrs);
  papplPrinterCloseDevice(printer);

  return NULL;
}


/*
 * 'tpcl_delete_cb()' - Callback for deleting a printer
 *
 * Cleans up printer resources including the persistent state file.
 */

void tpcl_delete_cb(
  pappl_printer_t          *printer,
  pappl_pr_driver_data_t   *data
)
{
  (void)data;

  papplLogPrinter(printer, PAPPL_LOGLEVEL_INFO, "Printer deleted, cleaning up resources");
  tpcl_state_delete(printer);
}


/*
 * 'tpcl_free_job_buffers()' - Free raster job buffers
 */

static void tpcl_free_job_buffers(
  pappl_job_t              *job,
  tpcl_job_t               *tpcl_job
)
{
  if (tpcl_job)
  {
    if (tpcl_job->buffer)  free(tpcl_job->buffer);
    if (tpcl_job->compbuf) tpcl_compbuf_delete(tpcl_job->compbuf);
    free(tpcl_job);
  }

  papplJobSetData(job, NULL);
  return;
}
