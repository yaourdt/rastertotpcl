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
// TODO: Replace memmove etc with papplCopyString
// TODO: Maximale Label länge programmieren: : A sensor calibration is possible up to 254-mm pitch media.
// TODO: Propper error handling wie zB Deckel offen
// TODO: Persist previous page size between job runs to use (T) command. two options:
//       - papplSystemSetSaveCallback() ,papplPrinterSetSaveCallback() <- needs printer dependence!
//       - papplPrinterAddAttr(printer, IPP_TAG_INTEGER, "myapp-my-var", value);
// TODO: Implement testpage callback
// TODO: Implement delete printer callback
// TODO: Clean out printer properties in tpcl_driver_cb
// TODO: Document, that this driver assumes, all pages in a job are of the same size
// TODO: Implement printer autodetection
// TODO set IPP attributes
// TODO test identify action

#include "tpcl-driver.h"
#include "dithering.h"


/*
 * TPCL Graphics Modes
 */

#define TEC_GMODE_NIBBLE_AND 0                           // Raw nibble AND mode (4 dots/byte encoded in ASCII)
#define TEC_GMODE_HEX_AND    1                           // Raw hex AND mode (8 dots/byte)
#define TEC_GMODE_TOPIX      3                           // TOPIX compression (default, recommended)
#define TEC_GMODE_NIBBLE_OR  4                           // Raw nibble OR mode (4 dots/byte encoded in ASCII)
#define TEC_GMODE_HEX_OR     5                           // Raw hex OR mode (8 dots/byte)


/*
 * Driver information array
 */

const pappl_pr_driver_t tpcl_drivers[] = {
  {
    "toshiba-tec-tpcl",                                  // Name
    "Toshiba TEC TPCL v2 Label Printer",                 // Description
    "MFG:Toshiba Tec;MDL:TPCL;CMD:TPCL;",                // IEEE-1284 device ID
    NULL                                                 // Extension
  }
};

const int tpcl_drivers_count = sizeof(tpcl_drivers) / sizeof(tpcl_drivers[0]);


/*
 * Job data structure
 */

typedef struct {
  int                      gmode;                        // Graphics mode (TOPIX, hex, nibble)
  int                      print_width;                  // Effective print width (0.1mm)
  int                      print_height;                 // Effective print height (0.1mm)
  int                      label_pitch;                  // Label pitch = print height + 5mm gap
  int                      roll_width;                   // Roll width  = print width + 5mm margin
  unsigned int             buffer_len;                   // Length of line buffer as sent to printer
  unsigned char            *buffer;                      // Current line buffer
  unsigned char            *last_buffer;                 // Previous line buffer (for TOPIX)
  unsigned char            *comp_buffer;                 // Compression buffer (for TOPIX)
  unsigned char            *comp_buffer_ptr;             // Current position in comp_buffer (for TOPIX)
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

static void tpcl_topix_compress(
  pappl_pr_options_t       *options,
  tpcl_job_t               *tpcl_job
);

static ssize_t tpcl_topix_output_buffer(
  pappl_pr_options_t       *options,
  pappl_device_t           *device,
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

  // Costom driver extensions
  //driver_data->extension;                              // Extension data

  // Set callbacks
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

  // Model-agnostic printer options
  dither_threshold16(driver_data->gdither, 128);         // dithering for 'auto', 'text', and 'graphic' TODO let user choose cutoff
  dither_bayer16(driver_data->pdither);                  // dithering for 'photo'
  driver_data->format = "application/vnd.toshiba-tpcl";  // Native file format
  driver_data->ppm = 10;                                 // Pages per minute (guesstimate)
  driver_data->ppm_color = 0;                            // No color printing
  //pappl_icon_t icons[3];                               // "printer-icons" values. TODO: Implement
  driver_data->kind = PAPPL_KIND_LABEL;		            	 // "printer-kind" values
  driver_data->has_supplies = false;                     // Printer can report supplies. TODO: Implement
  driver_data->input_face_up = true;		                 // Does input media come in face-up?
  driver_data->output_face_up = true;		                 // Does output media come out face-up?
  driver_data->orient_default = IPP_ORIENT_NONE;         // "orientation-requested-default" value
  driver_data->color_supported =                         // Highest supported color mode advertised via IPP
    PAPPL_COLOR_MODE_BI_LEVEL  |                         //  - black & white
    PAPPL_COLOR_MODE_MONOCHROME;                         //  - grayscale
  driver_data->color_default =                           // Default color mode 
    PAPPL_COLOR_MODE_BI_LEVEL;
  driver_data->content_default = PAPPL_CONTENT_AUTO;     // Optimize for vector graphics or image content
  driver_data->quality_default = IPP_QUALITY_NORMAL;     // "print-quality-default" value
  driver_data->scaling_default = PAPPL_SCALING_AUTO;     // "print-scaling-default" value
  driver_data->raster_types =                            // Supported color schemes by our driver callback
    PAPPL_PWG_RASTER_TYPE_BLACK_1 |                      // - black & white 1 bit
    PAPPL_PWG_RASTER_TYPE_BLACK_8 |                      // - black & white 8 bit
    PAPPL_PWG_RASTER_TYPE_SGRAY_8;                       // - grayscale 8 bit
  driver_data->force_raster_type = PAPPL_PWG_RASTER_TYPE_NONE;   // Force a particular raster type?
  driver_data->duplex = PAPPL_DUPLEX_NONE;               // Duplex printing modes supported
  driver_data->sides_supported = PAPPL_SIDES_ONE_SIDED;  // "sides-supported" values
  driver_data->sides_default = PAPPL_SIDES_ONE_SIDED;    // "sides-default" value
  driver_data->borderless = true;                        // Borderless margins supported?
  driver_data->left_right = 0;                           // Left and right margins in hundredths of millimeters
  driver_data->bottom_top = 0;                           // Bottom and top margins in hundredths of millimeters	

  // Model-specific printer options
  if (true) {                                            // TODO actually determine the printer
    strncpy(driver_data->make_and_model, "Toshiba TEC TPCL v2 Label Printer", // TODO use more specific name for each printer
      sizeof(driver_data->make_and_model) - 1);
    driver_data->make_and_model[sizeof(driver_data->make_and_model) - 1] = '\0';
    driver_data->finishings = PAPPL_FINISHINGS_NONE;     // "finishings-supported" values

    // Supported resolutions (203dpi = 8 dots/mm, 300dpi = 12 dots/mm, 600dpi = 24 dots/mm)
    driver_data->num_resolution = 2;                     // Number of printer resolutions
    driver_data->x_resolution[0] = 203;                  // Horizontal printer resolutions
    driver_data->x_resolution[1] = 300;
    driver_data->y_resolution[0] = 203;                  // Vertical printer resolutions
    driver_data->y_resolution[1] = 300;
    driver_data->x_default = 203;                        // Default horizontal resolution
    driver_data->y_default = 203;                        // Default vertical resolution

    // Common label sizes - using standard PWG media names - TODO clean and move to roll definitions, which currently does not work as PAPPL rejects the print job
    // Use roll media for label printers - allows any size within range
    // Range: 6x6mm (min) to 203x330mm (max)
    //driver_data->num_media = 2;
    //driver_data->media[0] = "roll_min_6x6mm";
    //driver_data->media[1] = "roll_max_203x330mm";
    //WORKAROUND We define a selection of common sizes instead of roll_min/roll_max
    // due to validation in PAPPL 1.4.9

    // Supported media
    // Use roll media range for label printers - allows any size within range
    int media_idx = 0; // TODO this could lead to an overflow as it ignores PAPPL_MAX_MEDIA
    driver_data->media[media_idx++] = "roll_min_6x6mm";  // Minimum label size
    driver_data->media[media_idx++] = "roll_max_203x330mm";// Maximum label size (8" x 13")
    driver_data->num_media = media_idx;                  // Number of supported media

    // Available media sources
    media_idx = 0;  // TODO this could lead to an overflow as it ignores PAPPL_MAX_SOURCE
    driver_data->source[media_idx++] = "main-roll";  // Media sources
	  driver_data->num_source = media_idx;             // Number of media sources (trays/rolls)

    // Available media types
    media_idx = 0;  // TODO this could lead to an overflow as it ignores PAPPL_MAX_TYPE
    driver_data->type[media_idx++] = "labels";                 // Media types
    driver_data->type[media_idx++] = "labels-continuous";
    driver_data->type[media_idx++] = "direct-thermal";
    driver_data->num_type = media_idx;               // Number of media types

    // Fill out ready media TODO replace by function that reads available media sizes from a definition file
    for (int i = 0; i < driver_data->num_source; i ++)
    {
      driver_data->media_ready[i].bottom_margin = driver_data->bottom_top; // Bottom margin in hundredths of millimeters
      driver_data->media_ready[i].left_margin   = driver_data->left_right; // Left margin in hundredths of millimeters
      driver_data->media_ready[i].left_offset   = 0;                       // Left offset in hundredths of millimeters
      driver_data->media_ready[i].right_margin  = driver_data->left_right; // Right margin in hundredths of millimeters

      // TODO all of the following should not be hard coded
      driver_data->media_ready[i].size_width    = 10300;                   // Width in hundredths of millimeters
      driver_data->media_ready[i].size_length   = 19900;                   // Height in hundredths of millimeters
      papplCopyString(driver_data->media_ready[i].size_name, "na_4x8_4x8in", sizeof(driver_data->media_ready[i].size_name)); // Use closest standard PWG media name

      papplCopyString(driver_data->media_ready[i].source, driver_data->source[i], sizeof(driver_data->media_ready[i].source)); // PWG media source name
      driver_data->media_ready[i].top_margin    = driver_data->bottom_top; // Top margin in hundredths of millimeters
      driver_data->media_ready[i].top_offset    = 0;                       // Top offset in hundredths of millimeters
      driver_data->media_ready[i].tracking      = 0;                       // IPP media tracking type, we have no tracking

      papplCopyString(driver_data->media_ready[i].type, driver_data->type[0],  sizeof(driver_data->media_ready[i].type)); // PWG media type name
    }

    driver_data->media_default = driver_data->media_ready[0];              // Default media
  }

  driver_data->left_offset_supported[0] = 0;             // media-left-offset-supported (0,0 for none)
  driver_data->left_offset_supported[1] = 0;
  driver_data->top_offset_supported[0] = 0;              // media-top-offset-supported (0,0 for none)
  driver_data->top_offset_supported[1] = 0;
  driver_data->num_bin = 0;                              // Number of output bins
  //driver_data->*bin[PAPPL_MAX_BIN];                    // Output bins
  //driver_data->bin_default;                            // Default output bin
  driver_data->mode_configured = 0;                      // label-mode-configured TODO set coorect flag
  driver_data->mode_supported = 0;                       // label-mode-supported TODO set correct flag
  driver_data->tear_offset_configured = 0;               // label-tear-offset-configured TODO set correct flag
  driver_data->tear_offset_supported[0] = 0;             // label-tear-offset-supported (0,0 for none) TODO set via user
  driver_data->tear_offset_supported[1] = 0;
  driver_data->speed_supported[0] = 2;                   // print-speed-supported (in inches per second?) (0,0 for none) TODO check value correctness
  driver_data->speed_supported[1] = 10;
  driver_data->speed_default = 3;                        // print-speed-default TODO check value correctness

  // TODO 
  //driver_data->darkness_default;                       // print-darkness-default
  //driver_data->darkness_configured;                    // printer-darkness-configured
  //driver_data->darkness_supported;                     // printer/print-darkness-supported (0 for none)
  //driver_data->identify_default;                       // "identify-actions-default" values PAPPL_IDENTIFY_ACTIONS_SOUND
  //driver_data->identify_supported;                     // "identify-actions-supported" values
  //driver_data->num_features;                           // Number of "ipp-features-supported" values
  //driver_data->*features[PAPPL_MAX_VENDOR];            // "ipp-features-supported" values
  //driver_data->num_vendor;                             // Number of vendor attributes
  //driver_data->*vendor[PAPPL_MAX_VENDOR];              // Vendor attribute names

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
    }
    else
    {
      // Log specific error conditions based on status code
      if (strcmp(status_code, "01") == 0)
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Top cover open");
      else if (strcmp(status_code, "03") == 0)
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Exclusively accessed by other host");
      else if (strcmp(status_code, "04") == 0)
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Paused");
      else if (strcmp(status_code, "05") == 0)
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Waiting for stripping");
      else if (strcmp(status_code, "06") == 0)
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Command error");
      else if (strcmp(status_code, "07") == 0)
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "RS-232C error");
      else if (strcmp(status_code, "11") == 0)
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Paper jam");
      else if (strcmp(status_code, "12") == 0)
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Paper jam at cutter");
      else if (strcmp(status_code, "13") == 0)
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "The label has run out");
      else if (strcmp(status_code, "15") == 0)
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Feed attempt while cover open");
      else if (strcmp(status_code, "16") == 0)
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Steeping motor overheat");
      else if (strcmp(status_code, "18") == 0)
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Thermal head overheat");
      else if (strcmp(status_code, "21") == 0)
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "The ribbon has run out");
      else if (strcmp(status_code, "23") == 0)
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Print succeeded. The label has run out");
      else if (strcmp(status_code, "50") == 0)
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "SD card write error");
      else if (strcmp(status_code, "51") == 0)
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "SD card format error");
      else if (strcmp(status_code, "54") == 0)
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "SD card full");
      else if (strcmp(status_code, "55") == 0)
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "PC command mode / initialize SD / EEPROM error");
      else
        papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Unknown status code: %s", status_code);
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
  char                     command[256];                 // Command buffer
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

  // Add 5mm (50 tenths of mm) to each dimension for label pitch and roll width - TODO implement as user setting
  label_pitch = print_height + 50;  // Label pitch = print height + 5mm gap
  roll_width  = print_width + 50;   // Roll width = print width + 5mm margin

  papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Calculated label dimensions: width=%d (0.1mm), height=%d (0.1mm), pitch=%d (0.1mm), roll=%d (0.1mm)", print_width, print_height, label_pitch, roll_width);

  // Validate dimensions are within printer limits
  // 203dpi: pitch max 9990 (999.0mm), height max 9970 (997.0mm)
  // 300dpi: pitch max 4572 (457.2mm), height max 4552 (455.2mm)
  int max_pitch = (driver_data.y_default == 300) ? 4572 : 9990;
  int max_height = (driver_data.y_default == 300) ? 4552 : 9970;

  if (label_pitch > max_pitch)
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Label pitch %d (0.1mm) exceeds maximum %d (0.1mm) for %ddpi resolution", label_pitch, max_pitch, driver_data.y_default);
    papplPrinterCloseDevice(printer);
    return;
  }

  if (print_height > max_height)
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Print height %d (0.1mm) exceeds maximum %d (0.1mm) for %ddpi resolution", print_height, max_height, driver_data.y_default);
    papplPrinterCloseDevice(printer);
    return;
  }

  // Send label size command
  snprintf(
    command,
    sizeof(command),
    "{D%04d,%04d,%04d,%04d|}\n",
    label_pitch,
    print_width,
    print_height,
    roll_width
  );
  papplDevicePuts(device, command);
  papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Sending label size command: %s", command);

  // Send feed command

  /*
  [ESC] Tabcde [LF] [NUL]
  
a: Type of sensor
0: No sensor
1: Reflective sensor
2: Transmissive sensor (when using normal labels)
3: Transmissive sensor (when using normal labels)
4: Reflective sensor

b: Selects cut or non-cut
0: Non-cut
1: Cut

c: Feed mode
C: Batch mode (Cut and feed when “Cut” is selected for parameter b.)
D: Strip mode (with back feed)
E: Strip mode (Reserved for future)
F: Partial cut mode (Non back feed cut mode)

d: Feedspeed
1: 2 inches/sec (2 inches/sec for the 300 dpi model)
2: 2 inches/sec (2 inches/sec for the 300 dpi model)
3: 3 inches/sec (3 inches/sec for the 300 dpi model)
4: 4 inches/sec (4 inches/sec for the 300 dpi model)
5: 5 inches/sec (4 inches/sec for the 300 dpi model)
6: 5 inches/sec (4 inches/sec for the 300 dpi model)
7: 5 inches/sec (4 inches/sec for the 300 dpi model)
8: 5 inches/sec (4 inches/sec for the 300 dpi model)
9: 5 inches/sec (4 inches/sec for the 300 dpi model)
A: 5 inches/sec (4 inches/sec for the 300 dpi model)
B: 5 inches/sec. (4 inches/sec for the 300 dpi model)

e: With/without ribbon
Direct thermal models:
Set to 0. (If “1” or “2” is specified, a ribbon error results.)
Thermal transfer models:
0: Without ribbon
1: With ribbon
2: With ribbon

*/
  snprintf(
    command,
    sizeof(command),
    "{T20C10|}\n"
     //TODO: actually calculate from label size and other settings
  );
  papplDevicePuts(device, command);
  papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Sending feed command: %s", command);

  papplDeviceFlush(device);
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
      papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Line too long (exceeds %lu bytes)", sizeof(line));
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

  char command[256];

  // Allocate and set the per-job driver data pointer
  tpcl_job_t *tpcl_job;
  tpcl_job = (tpcl_job_t *)calloc(1, sizeof(tpcl_job_t));
  if (!tpcl_job)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Failed to allocate job data structure");
    return false;
  }
  papplJobSetData(job, tpcl_job);

  // Set graphics mode 
  // TODO: Add support for TOPIX compression and let user choose. for not 150 or 300 dpi, do not allow topix mode
  //tpcl_job->gmode = TEC_GMODE_NIBBLE_AND;
  //tpcl_job->gmode = TEC_GMODE_HEX_AND;
  tpcl_job->gmode = TEC_GMODE_TOPIX;

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
    tpcl_job->buffer      = malloc(options->header.cupsBytesPerLine);
    tpcl_job->last_buffer = malloc(options->header.cupsBytesPerLine);
    tpcl_job->comp_buffer = malloc(0xFFFF);

    if (!tpcl_job->buffer || !tpcl_job->last_buffer || !tpcl_job->comp_buffer)
    {
      papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Failed to allocate TOPIX compression buffers");
      tpcl_free_job_buffers(job, tpcl_job);
      return false;
    }
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "TOPIX buffers allocated: line=%u bytes, comp=65535 bytes", options->header.cupsBytesPerLine);
  }
  else
  {
    // Allocate buffer for hex or nibble modes
    tpcl_job->buffer = malloc(options->header.cupsBytesPerLine);

    if (!tpcl_job->buffer)
    {
      papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Failed to allocate line buffer for HEX / Nibble mode");
      tpcl_free_job_buffers(job, tpcl_job);
      return false;
    }
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "HEX mode buffer allocated: line=%u bytes", options->header.cupsBytesPerLine);
  }

  // Calculate label dimensions from page size
  // cupsPageSize is in points (1/72 inch), convert to 0.1mm: points * 254 / 72
  tpcl_job->print_width  = (int)(options->header.cupsPageSize[0] * 254.0 / 72.0);  // Effective print width (0.1mm)
  tpcl_job->print_height = (int)(options->header.cupsPageSize[1] * 254.0 / 72.0);  // Effective print height (0.1mm)

  // Add 5mm (50 tenths of mm) margins TODO let user set this
  tpcl_job->label_pitch = tpcl_job->print_height + 50;  // Label pitch = print height + 5mm gap
  tpcl_job->roll_width  = tpcl_job->print_width + 50;   // Roll width  = print width + 5mm margin

  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Calculated label dimensions from page size: width=%d (0.1mm), height=%d (0.1mm), pitch=%d (0.1mm), roll=%d (0.1mm)", tpcl_job->print_width, tpcl_job->print_height, tpcl_job->label_pitch, tpcl_job->roll_width);
  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Maximum image resolution at %ux%udpi: width=%u dots, height=%u dots", options->header.HWResolution[0], options->header.HWResolution[1], (unsigned int) (options->header.HWResolution[0] * options->header.cupsPageSize[0] / 72), (unsigned int) (options->header.HWResolution[1] * options->header.cupsPageSize[1] / 72));

  // Calculate buffer length in bytes as sent to printer
  tpcl_job->buffer_len = options->header.cupsBytesPerLine / options->header.cupsBitsPerPixel;

  // Validate dimensions are within printer limits before sending label size command
  // 203dpi: pitch max 9990 (999.0mm), height max 9970 (997.0mm)
  // 300dpi: pitch max 4572 (457.2mm), height max 4552 (455.2mm)
  int max_pitch = (options->header.HWResolution[1] == 300) ? 4572 : 9990;
  int max_height = (options->header.HWResolution[1] == 300) ? 4552 : 9970;

  if (tpcl_job->label_pitch > max_pitch)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Label pitch %d (0.1mm) exceeds maximum %d (0.1mm) for %udpi resolution", tpcl_job->label_pitch, max_pitch, options->header.HWResolution[1]);
    tpcl_free_job_buffers(job, tpcl_job);
    return false;
  }

  if (tpcl_job->print_height > max_height)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Print height %d (0.1mm) exceeds maximum %d (0.1mm) for %udpi resolution", tpcl_job->print_height, max_height, options->header.HWResolution[1]);
    tpcl_free_job_buffers(job, tpcl_job);
    return false;
  }

  // Send label size command
  snprintf(
    command,
    sizeof(command),
    "{D%04d,%04d,%04d,%04d|}\n",
    tpcl_job->label_pitch,                               // label pitch
    tpcl_job->print_width,                               // label width
    tpcl_job->print_height,                              // label length
    tpcl_job->roll_width                                 // full width of label roll
  );
  papplDevicePuts(device, command);
  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending label size command: %s", command);

  // Send feed adjustment command - TODO only send when necessary; current command format throws error; should be configurable via job options
  if (false)
  {
    snprintf(
      command,
      sizeof(command),
      "{AX%04d,%04d,%04d,%04d|}\n",  // TODO "{AX;+00,+00,+00|}\n" 
      0,
      0,
      0,
      0
    );
    papplDevicePuts(device, command);
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending feed adjustment command: %s", command);
  }

  // Print density adjustment command - TODO only send when necessary; untested; should be configurable via job options
  if (false)
  {
    snprintf(
      command,
      sizeof(command),
      "{AY%04d,%04d,%04d,%04d|}\n",  // TODO "{AY;+00,0|}\n"
      0,
      0,
      0,
      0
    );
    papplDevicePuts(device, command);
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending print density adjustment command: %s", command);
  }
 
  // If label size changed, send feed command - TODO determine label size change
  if (false)
  {
    snprintf(
      command,
      sizeof(command),
      "{T20C10|}\n" //TODO: actually calculate from label size and other settings
    );
    papplDevicePuts(device, command);
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending feed command: %s", command);
  }

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

  char command[256];

  // Fetch the job driver data pointer
  tpcl_job_t *tpcl_job = (tpcl_job_t *)papplJobGetData(job);
  if (!tpcl_job)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Job data structure not initialized");
    return false;
  }

  // Clear image buffer command
  papplDevicePuts(device, "{C|}\n");
  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending clear image buffer command: {C|}\n");

  if (tpcl_job->gmode == TEC_GMODE_TOPIX)
  {
    // Zero buffers in case of TOPIX compression
    memset(tpcl_job->last_buffer, 0, options->header.cupsBytesPerLine);
    memset(tpcl_job->comp_buffer, 0, 0xFFFF);

    tpcl_job->comp_buffer_ptr = tpcl_job->comp_buffer;
    tpcl_job->y_offset = 0;

    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "TOPIX buffers zeroed: line=%u bytes, comp=65535 bytes", options->header.cupsBytesPerLine);
  }
  else
  {
    // For hex and nibble mode, send the SG command header
    snprintf(
      command,
      sizeof(command),
      "{SG;0000,00000,%04u,%05u,%d,",
                                                         // x_origin TODO margin
                                                         // y_origin TODO margin
      options->header.cupsWidth,                         // width_dots
      options->header.cupsHeight,                        // height_dots
      tpcl_job->gmode                                    // graphics mode
    );
    papplDevicePuts(device, command);
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending graphic command header (width, height, mode): %s", command);

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
      papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Line %u: Transmitting %u bytes in hex mode", y, tpcl_job->buffer_len);
    }

    papplDeviceWrite(device, tpcl_job->buffer, tpcl_job->buffer_len);
  }
  else if ((tpcl_job->gmode == TEC_GMODE_NIBBLE_AND) | (tpcl_job->gmode == TEC_GMODE_NIBBLE_OR))
  {
    // Mode to transmit data encoded as ASCII characters '0' (0x30, 0b0011 0000) to '?' (0x3F, 0b0011 1111)
    // Split incoming buffer into high and low nibble, prefix 0b0011 high nibble for both bytes and send

    if ((y < 3) | (y > (options->header.cupsHeight - 4)))
    {
      papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Line %u: Transmitting %u bytes in nibble mode (ASCII mode)", y, tpcl_job->buffer_len * 2);
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
      papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Line %u: Compressing %u bytes in TOPIX mode", y, tpcl_job->buffer_len);
    }

    // Compress line using TOPIX algorithm
    tpcl_topix_compress(options, tpcl_job);

    // Check if compression buffer is getting full, flush if needed. Also flush if this is the last line
    size_t buffer_used      = tpcl_job->comp_buffer_ptr - tpcl_job->comp_buffer;
    size_t buffer_threshold = 0xFFFF - (tpcl_job->buffer_len + ((tpcl_job->buffer_len / 8) * 3));

    if ((buffer_used > buffer_threshold) | (y == (options->header.cupsHeight - 1)))
    {
      papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Line %u: TOPIX buffer full (%lu/65535 bytes) or last line, flushing. Y offset for this image: %d (0.1mm)", y, buffer_used, tpcl_job->y_offset);
      
      ssize_t bytes_written = tpcl_topix_output_buffer(options, device, tpcl_job);

      // Y offset for next image in 0.1mm (for TOPIX)
      tpcl_job->y_offset = (y + 1) * 254 / options->header.HWResolution[0];

      papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Line %u: TOPIX buffer flushed, %lu bytes sent. Y offset for next image: %d (0.1mm)", y, bytes_written, tpcl_job->y_offset);
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
 *   3. TCP workaround: 1024 spaces padding
 *   4. BEV4T workaround: Send 600 null bytes as dummy data 
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

  char command[256];

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

  // Print the label
  snprintf(
    command,
    sizeof(command),
    "{XS;I,0001,0002C3000|}\n" //TODO: actually calculate from job data
  );
  papplDevicePuts(device, command);
  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending issue label command: %s", command);
  // Format: {XS;I,copies,cut(3)+detect(1)+mode(1)+speed(1)+media(1)+mirror(1)+status(1)|}
  // Parameters:
  //   cut: 000-999 (cut quantity)
  //   detect: 0-4 (media tracking/detect mode)
  //   mode: C=tear-off, D=peel-off, E=rewind
  //   speed: 2-A (2,3,4,5,6,8,A=10)
  //   media: 0=direct thermal, 1=thermal transfer, 2=ribbon saving
  //   mirror: 0=normal, 1=mirror
  //   status: 0=no status, 1=with status
  //snprintf(command, sizeof(command),
  //         "{XS;I,%04d,000%d%c%d%d%d%d|}\n",
  //         options->copies,
  //         0,    // detect (media tracking) - TODO: make configurable
  //         'C',  // mode: C=tear-off, D=peel-off, E=rewind - TODO: make configurable
  //         3,    // speed - TODO: use options->print_speed
  //         1,    // media: thermal transfer - TODO: make configurable
  //         0,    // mirror
  //         0);   // status

  // Send padding to avoid TCP zero-window error (workaround) - TODO understand and only send when applicable
  //papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending 1024 space padding (TCP workaround)");
  //papplDevicePrintf(device, "%1024s", "");

  // Send 600 null bytes dummy data (BEV4T last packet lost bug workaround) - TODO understand and only send when applicable
  //static unsigned char dummy_data[600] = {0};  // 600 null bytes for BEV4T workaround
  //papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending 600 null bytes (BEV4T workaround)");
  //papplDeviceWrite(device, dummy_data, sizeof(dummy_data));

  papplDeviceFlush(device);

  return true;
}


/*
 * 'tpcl_rendjob_cb()' - End a job
 *
 * No specific commands sent at end of job, just cleanup job data structure.
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

  // Free buffers
  tpcl_free_job_buffers(job, tpcl_job);
  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Freeing page buffers and job data structure");

  return true;
}


/*
 * 'tpcl_testpage_cb()' - Print test file callback
 *
 * Currently not implemented, just returns NULL. TODO
 */

const char* tpcl_testpage_cb(
  pappl_printer_t          *printer,
  char                     *buffer,
  size_t                   bufsize
)
{
  papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Test file printing currently not implemented");
  return NULL;
}


/*
 * 'tpcl_delete_cb()' - Callback for deleting a printer
 *
 * Currently not implemented, just exists so server does not crash. TODO
 */

void tpcl_delete_cb(
  pappl_printer_t          *printer,
  pappl_pr_driver_data_t   *data
)
{
  return;
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
    if (tpcl_job->buffer)      free(tpcl_job->buffer);
    if (tpcl_job->last_buffer) free(tpcl_job->last_buffer);
    if (tpcl_job->comp_buffer) free(tpcl_job->comp_buffer);
    free(tpcl_job);
  }

  papplJobSetData(job, NULL);
  return;
}


/*
 * 'tpcl_topix_compress()' - Compress a line using TOPIX algorithm
 *
 * TOPIX is a 3-level hierarchical compression that performs XOR
 * with the previous line and only transmits changed bytes.
 */

static void
tpcl_topix_compress(
  pappl_pr_options_t       *options,
  tpcl_job_t               *tpcl_job
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

  width = tpcl_job->buffer_len;

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
        xor = tpcl_job->buffer[i] ^ tpcl_job->last_buffer[i];
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
  *tpcl_job->comp_buffer_ptr = cl1;
  tpcl_job->comp_buffer_ptr++;

  // Copy only non-zero bytes to compressed buffer
  if (cl1 > 0)
  {
    ptr = &line[0][0][0];
    for (i = 0; i < max; i++)
    {
      if (*ptr != 0)
      {
        *tpcl_job->comp_buffer_ptr = *ptr;
        tpcl_job->comp_buffer_ptr++;
      }
      ptr++;
    }
  }

  // Copy current line to last_buffer for next iteration
  memcpy(tpcl_job->last_buffer, tpcl_job->buffer, width);
}


/*
 * 'tpcl_topix_output_buffer()' - Send TOPIX compressed data to printer
 */

static ssize_t
tpcl_topix_output_buffer(
  pappl_pr_options_t       *options,
  pappl_device_t           *device,
  tpcl_job_t               *tpcl_job
)
{
  char                     command[256];
  unsigned short           len, belen;                   // Buffer length and big endian buffer length
  ssize_t                  bytes_written = 0;

  len   = (unsigned short)(tpcl_job->comp_buffer_ptr - tpcl_job->comp_buffer);
  belen = (len << 8) | (len >> 8);                       // Convert length to big-endian (network byte order)
  if (len == 0) { return bytes_written; }

  // Send SG command with compressed data
  snprintf(
    command,
    sizeof(command),
    "{SG;0000,%05d,%04u,%05u,%d,",
                                                         // x_origin in 0.1mm TODO margin
    tpcl_job->y_offset,                                  // y_origin in 0.1mm TODO margin
    options->header.cupsWidth,                           // width_dots
    options->header.HWResolution[0],                     // in TOPIX mode: Resolution of graphic data (150 or 300 dpi)
    tpcl_job->gmode                                      // graphics mode
  );

  bytes_written += papplDevicePuts (device, command);
  bytes_written += papplDeviceWrite(device, &belen, 2);                   // Total length of graphic data
  bytes_written += papplDeviceWrite(device, tpcl_job->comp_buffer, len);  // Compressed data
  bytes_written += papplDevicePuts (device, "|}\n");

  // Reset buffers and pointers
  memset(tpcl_job->comp_buffer, 0, 0xFFFF);
  memset(tpcl_job->last_buffer, 0, tpcl_job->buffer_len);
  tpcl_job->comp_buffer_ptr = tpcl_job->comp_buffer;

  return bytes_written;
}
