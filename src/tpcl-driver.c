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
// TODO: Implement identification callback
// TODO: Implement raw print callback
// TODO: Implement testpage callback
// TODO: Implement delete printer callback
// TODO: Clean out printer properties in tpcl_driver_cb
// TODO: Document, that this driver assumes, all pages in a job are of the same size
// TODO: Implement printer autodetection
// TODO: in rwriteline check if the receive buffer is full, then abort
// TODO: in rwriteline add dithering

// Start kommende Woche: Problem mit der Buffer allocation ab Zeile 557 beheben. 

#include <string.h>
#include <math.h>
#include <unistd.h>
#include "tpcl-driver.h"
#include "dithering.h"


/*
 * TPCL Graphics Modes
 */

#define TEC_GMODE_HEX_AND 1                              // Raw hex AND mode
#define TEC_GMODE_TOPIX   3                              // TOPIX compression (default, recommended)
#define TEC_GMODE_HEX_OR  5                              // Raw hex OR mode


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
  unsigned char            *buffer;                      // Current line buffer
  unsigned char            *last_buffer;                 // Previous line buffer (for TOPIX)
  unsigned char            *comp_buffer;                 // Compression buffer
  unsigned char            *comp_buffer_ptr;             // Current position in comp_buffer
  int                      comp_last_line;               // Last line number sent
  int                      page;                         // Current page number
  int                      gmode;                        // Graphics mode (TOPIX/HEX)
  int                      width;                        // Line width in bytes  - TODO not correct 
  int                      height;                       // Page height in lines - TODO not correct
  int                      gap;                          // Page height in lines - TODO not correct
  int                      peel;                         // Page height in lines - TODO not correct
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

static void tpcl_topix_compress(
  tpcl_job_t               *tpcl_job,
  int                      y
);

static void tpcl_topix_output_buffer(
  pappl_device_t           *device,
  tpcl_job_t               *tpcl_job,
  int                      y
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
  ipp_t                    **driver_attrs,               // Driver attributes
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
  driver_data->color_supported = PAPPL_COLOR_MODE_MONOCHROME;    // Highest supported color mode
  driver_data->color_default = PAPPL_COLOR_MODE_MONOCHROME;      // Default color mode
  driver_data->content_default = PAPPL_CONTENT_AUTO;     // "print-content-default" value
  driver_data->quality_default = IPP_QUALITY_NORMAL;     // "print-quality-default" value
  driver_data->scaling_default = PAPPL_SCALING_AUTO;     // "print-scaling-default" value
  driver_data->raster_types = PAPPL_PWG_RASTER_TYPE_BLACK_1 |    // IPP supported color schemes / raster types TODO check if correct, missing grayscale
                              PAPPL_PWG_RASTER_TYPE_BLACK_8;
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
  //driver_data->identify_default;                       // "identify-actions-default" values
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
    papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Timeout waiting for status response (%d ms)", max_attempts);
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
    papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Status response: '%s' after %is", status_code, poll_attempts);

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
 * Currently not implemented. TODO
 */

void tpcl_identify_cb(
  pappl_printer_t          *printer, 
  pappl_identify_actions_t actions, 
  const char               *message
)
{
  papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Printer identification currently not implemented");
  return;
}


/*
 * 'tpcl_print_cb()' - Print raw file callback
 *
 * Currently not implemented, just returns false. TODO
 */

bool
tpcl_print_cb(
  pappl_job_t              *job, 
  pappl_pr_options_t       *options, 
  pappl_device_t           *device
)
{
  papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Raw file printing currently not implemented for TPCL V2 driver");
  return false;
}


/*
 * 'tpcl_rstartjob_cb()' - Start a print job
 *
 * Creates job data structure and sends job initialization commands:
 *   1. (if labe size changed) {Daaaa,bbbb,cccc,dddd|} -> Label size definition
 *   2. (if not zero) {AX;abbb,cddd,eff|} -> Position fine adjustment
 *   3. (if not zero) {AY;abb,c|} -> Print density fine adjustment
 *   4. (if labe size changed) {Tabcde|} -> Feed page
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

  // Calculate label dimensions in 0.1mm units (TPCL format)
  // cupsPageSize is in points (1/72 inch), convert to 0.1mm: points * 254 / 72
  tpcl_job->width  = (int)(options->header.cupsPageSize[0] * 254.0 / 72.0); // TODO check if this should be cupsImagingBBox
  tpcl_job->height = (int)(options->header.cupsPageSize[1] * 254.0 / 72.0); // TODO check if this should be cupsImagingBBox
  tpcl_job->gap    = 30; // 3mm default gap between labels TODO let user set this -> cupsPageSize?
  tpcl_job->peel   = 30; // 3mm default gap between label and side of roll TODO let user set this -> cupsPageSize?

  // Determine graphics mode (default to TOPIX for best compression) - TODO let user choose
  tpcl_job->gmode = 1; //TEC_GMODE_TOPIX; TODO
  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Using graphics mode: %d (%s)",
              tpcl_job->gmode,
              tpcl_job->gmode == TEC_GMODE_TOPIX ? "TOPIX" :
              tpcl_job->gmode == TEC_GMODE_HEX_AND ? "HEX_AND" : "HEX_OR");

  if (tpcl_job->gmode == TEC_GMODE_TOPIX)
  {
    // Allocate buffers for TOPIX compression
    tpcl_job->buffer      = malloc(tpcl_job->width); // TODO this is incorrect, is 0.1mm should be bytes (pixel * ...)
      //tpcl_job->width = options->header.cupsBytesPerLine;
      //tpcl_job->height = options->header.cupsHeight;
    tpcl_job->last_buffer = malloc(tpcl_job->width); // TODO this is incorrect
    tpcl_job->comp_buffer = malloc(0xFFFF);          // TODO check if it makes sense to align with devices free buffer from status message

    if (!tpcl_job->buffer || !tpcl_job->last_buffer || !tpcl_job->comp_buffer)
    {
      if (tpcl_job->buffer)      free(tpcl_job->buffer);
      if (tpcl_job->last_buffer) free(tpcl_job->last_buffer);
      if (tpcl_job->comp_buffer) free(tpcl_job->comp_buffer);
      papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Failed to allocate TOPIX compression buffers");
      return false;
    }
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "TOPIX buffers allocated: line=%d bytes, comp=65535 bytes", tpcl_job->width);
  }
  else
  {
    // Allocate buffer for hex mode
    tpcl_job->buffer = malloc(tpcl_job->width);  // TODO this is incorrect, is 0.1mm should be bytes (pixel * ...)

    if (!tpcl_job->buffer)
    {
      papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Failed to allocate line buffer for HEX mode");
      return false;
    }

    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "HEX mode buffer allocated: line=%d bytes", tpcl_job->width);
  }

  // If label size changed, send label size command - TODO determine label size change
  if (true)
  {
    snprintf(
      command,
      sizeof(command),
      "{D%04d,%04d,%04d,%04d|}\n",
      2030,                                              // label pitch
      1030,                                              // label width
      1990,                                              // label length
      1070                                               // full width of label roll
    );
    papplDevicePuts(device, command);
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending label size command: %s", command);
  }

  // Send feed adjustment command - TODO only send when necessary; current command format throws error; should be configurable via job options
  if (false)
  {
    snprintf(
      command,
      sizeof(command),
      "{D%04d,%04d,%04d,%04d|}\n",  // TODO "{AX;+00,+00,+00|}\n" 
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
      "{D%04d,%04d,%04d,%04d|}\n",  // TODO "{AY;+00,0|}\n"
      0,
      0,
      0,
      0
    );
    papplDevicePuts(device, command);
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending print density adjustment command: %s", command);
  }
 
  // If label size changed, send feed command - TODO determine label size change
  if (true)
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

  // Update job data
  tpcl_job->page = page;

  // Zero buffers in case of TOPIX compression TODO
  if (false)
  {
    memset(tpcl_job->last_buffer, 0, tpcl_job->width); //TODO incorrect
    memset(tpcl_job->comp_buffer, 0, 0xFFFF);          //TODO incorrect

    tpcl_job->comp_buffer_ptr = tpcl_job->comp_buffer;
    tpcl_job->comp_last_line = 0;

    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "TOPIX buffers zeroed: line=%d bytes, comp=65535 bytes", tpcl_job->width);
  }

  // Clear image buffer command
  snprintf(
    command,
    sizeof(command),
    "{C|}\n"
  );
  papplDevicePuts(device, command);
  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending clear image buffer command: %s", command);

  return true;
}


/*
 * 'tpcl_rwriteline_cb()' - Write a line of raster data
 *
 * Dithers and compresses each line of data, then sends it to the device.
 * 
 * With TOPIX compression 
 *   1. {SG0;aaaa,bbbb,cccc,dddd,e,ffff,... -> Image headers
 *   2. ...ggg---ggg... -> Compressed image body
 *   3. ...|} -> Command footer
 * 
 * In HEX mode without compression
 *   1. {SG;aaaa,bbbb,cccc,dddd,e,... -> Image headers
 *   2. ...ggg---ggg...  -> Image body
 *   3. ...|} -> Command footer
 * 
 * If the image is larger than the available buffer, end the command, send it and start a new command
 * with updated y-coordinates.
 * For TOPIX compression, there is a upper limit to the buffer size of 0xFFFF (approx. 65 kb) due to indexing. 
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
  if (y < 4)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Starting line %u (logging this message for the first 3 lines only)", y);
  }

  // Fetch the job driver data pointer
  tpcl_job_t *tpcl_job = (tpcl_job_t *)papplJobGetData(job);
  if (!tpcl_job || !tpcl_job->buffer)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Line %u: Job data or buffer not initialized", y);
    return false;
  }

  // Copy line to buffer
  memcpy(tpcl_job->buffer, line, tpcl_job->width);

  if (tpcl_job->gmode == TEC_GMODE_TOPIX)
  {
    // Headers with TOPIX compression

//[ESC] SG; 0100, 0240, 0019, 0300, 3, 00 5C 80 80 40 30
//80 80 40 08 80 80 40 04 80 80 40 02 80 80 40 09
//80 80 60 04 80 80 80 60 02 40 80 80 40 01 80 80 20 20
//80 80 20 80 80 80 20 80 80 80 20 20 80 80 40 01
//80 80 60 02 40 80 80 A0 0F 80 80 80 C0 30 C3 80 80 80 40
//80 80 80 80 80 80 40 10 00 80 80 C0 80 20 80 80 C0 40 C0 [LF] [NUL]

    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "TOPIX mode header sent");
    // Compress using TOPIX algorithm
    tpcl_topix_compress(tpcl_job, y);

    // Check if compression buffer is getting full, flush if needed
    // Buffer size is 0xFFFF, flush when less than (width + (width/8)*3) bytes remaining
    size_t buffer_used = tpcl_job->comp_buffer_ptr - tpcl_job->comp_buffer;
    size_t buffer_threshold = 0xFFFF - (tpcl_job->width + ((tpcl_job->width / 8) * 3));

    if (buffer_used > buffer_threshold)
    {
      papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Line %u: TOPIX buffer full (%zu bytes), flushing",
                  y, buffer_used);
      //tpcl_topix_output_buffer(device, tpcl_job, y);
      memset(tpcl_job->last_buffer, 0, tpcl_job->width);
    }
  }
  else
  {
    // Headers for hex mode
//    snprintf(command, sizeof(command),
//             "{SG;0000,0000,%04d,%04d,%d,",
//             tpcl_job->width * 8, tpcl_job->height, tpcl_job->gmode);
//    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending HEX mode graphics header: %s", command);
//    papplDevicePuts(device, command);

    papplDevicePuts(device, "{SG;0100,0240,0019,0022,0,003000003800003<00003>000037000033800031<00030<00030>00030600030>00030<00031<00033800?33003??0007??000???000??>000??>0007?<0003?0000|}\n");
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "HEX mode header sent");
    // HEX mode: Output raw binary data directly
    //papplDeviceWrite(device, tpcl_job->buffer, tpcl_job->width);
  }

  return true;
}


/*
 * 'tpcl_rendpage_cb()' - End a page
 *
 * Sends page finalization commands:
 *   1. {XS;I,aaaa,bbbcdefgh|} - Execute print command
 *   2. TCP workaround: 1024 spaces padding
 *   3. BEV4T workaround: Send 600 null bytes as dummy data 
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
  static unsigned char dummy_data[600] = {0};  // 600 null bytes for BEV4T workaround

  // Fetch the job driver data pointer
  tpcl_job_t *tpcl_job = (tpcl_job_t *)papplJobGetData(job);
  if (!tpcl_job)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Job data structure not initialized");
    return false;
  }

  // Flush any remaining TOPIX data or close hex graphics
  if (tpcl_job->gmode == TEC_GMODE_TOPIX)
  {
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Flushing final TOPIX buffer");
    //tpcl_topix_output_buffer(device, tpcl_job, 0);
  }
  else
  {
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Closing HEX graphics data");
    //papplDevicePuts(device, "|}\n");
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
  //papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending print command: %s", command);
  //papplDevicePuts(device, command);

  // Send padding to avoid TCP zero-window error (workaround)
  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending 1024 space padding (TCP workaround)");
  papplDevicePrintf(device, "%1024s", "");

  // Send 600 null bytes dummy data (BEV4T last packet lost bug workaround)
  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending 600 null bytes (BEV4T workaround)");
  papplDeviceWrite(device, dummy_data, sizeof(dummy_data));

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
  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Freeing page buffers and job data structure");
  if (tpcl_job->buffer)
  {
    free(tpcl_job->buffer);
    tpcl_job->buffer = NULL;
  }
  if (tpcl_job->last_buffer)
  {
    free(tpcl_job->last_buffer);
    tpcl_job->last_buffer = NULL;
  }
  if (tpcl_job->comp_buffer)
  {
    free(tpcl_job->comp_buffer);
    tpcl_job->comp_buffer = NULL;
  }

  papplJobSetData(job, NULL);
  free(tpcl_job);
  return true;
}


/*
 * 'tpcl_testpage_cb()' - Print test file callback
 *
 * Currently not implemented, just returns NULL. TODO
 */

const char* tpcl_testpage_cb(
    pappl_printer_t *printer,
    char *buffer,
    size_t bufsize)
{
  printf("Test file printing currently not implemented!");
  return NULL;
}


/*
 * 'tpcl_delete_cb()' - Callback for deleting a printer
 *
 * Currently not implemented, just exists so server does not crash. TODO
 */

void tpcl_delete_cb(
    pappl_printer_t *printer,
    pappl_pr_driver_data_t *data)
{
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
  tpcl_job_t               *tcpl_job,
  int                      y //TODO remove not needed
)
{
  int           i, l1, l2, l3, max, width;
  unsigned char line[8][9][9] = {{{0}}};
  unsigned char cl1, cl2, cl3, xor;
  unsigned char *ptr;

  width = tcpl_job->width;
  max = 8 * 9 * 9;

  // Perform XOR with previous line for differential compression
  // Build 3-level index structure: Level1[8], Level2[9], Level3[9]
  cl1 = 0;
  i = 0;

  for (l1 = 0; l1 <= 7 && i < width; l1++)
  {
    cl2 = 0;
    for (l2 = 1; l2 <= 8 && i < width; l2++)
    {
      cl3 = 0;
      for (l3 = 1; l3 <= 8 && i < width; l3++, i++)
      {
        xor = tcpl_job->buffer[i] ^ tcpl_job->last_buffer[i];
        line[l1][l2][l3] = xor;
        if (xor > 0)
        {
          // Mark that this byte has changed
          cl3 |= (1 << (8 - l3));
        }
      }
      line[l1][l2][0] = cl3;
      if (cl3 != 0)
        cl2 |= (1 << (8 - l2));
    }
    line[l1][0][0] = cl2;
    if (cl2 != 0)
      cl1 |= (1 << (7 - l1));
  }

  // Always add CL1 byte to compressed buffer
  *tcpl_job->comp_buffer_ptr = cl1;
  tcpl_job->comp_buffer_ptr++;

  // Copy only non-zero bytes to compressed buffer
  if (cl1 > 0)
  {
    ptr = &line[0][0][0];
    for (i = 0; i < max; i++)
    {
      if (*ptr != 0)
      {
        *tcpl_job->comp_buffer_ptr = *ptr;
        tcpl_job->comp_buffer_ptr++;
      }
      ptr++;
    }
  }

  // Copy current line to last_buffer for next iteration
  memcpy(tcpl_job->last_buffer, tcpl_job->buffer, width);
}


/*
 * 'tpcl_topix_output_buffer()' - Send TOPIX compressed data to printer
 */

static void
tpcl_topix_output_buffer(
    pappl_device_t *device,
    tpcl_job_t     *tpcl_job,
    int            y)
{
  unsigned short len, belen;
  char           command[256];

  len = (unsigned short)(tpcl_job->comp_buffer_ptr - tpcl_job->comp_buffer);

  // DEBUG: Always log buffer state
  fprintf(stderr, "DEBUG tpcl_topix_output_buffer: len=%u, y=%d\n", len, y);

  if (len == 0)
  {
    fprintf(stderr, "DEBUG tpcl_topix_output_buffer: Buffer empty, returning\n");
    return;
  }

  // Convert length to big-endian (network byte order)
  belen = (len << 8) | (len >> 8);

  // Send SG command with compressed data
  // Format: {SG;x,y,width,height,mode,length+data|}
  // Calculate height: if final flush (y=0), send total height, otherwise send number of lines since last flush
  int height = y ? (y - tpcl_job->comp_last_line) : tpcl_job->height;

  snprintf(command, sizeof(command),
           "{SG;0000,%04d,%04d,%04d,%d,",
           tpcl_job->comp_last_line,
           tpcl_job->width * 8,
           height,
           tpcl_job->gmode);

  fprintf(stderr, "DEBUG: Sending TOPIX command: %s\n", command);
  fprintf(stderr, "DEBUG: Sending %u bytes of compressed data (big-endian len: 0x%04x)\n", len, belen);

  ssize_t bytes_written = 0;
  bytes_written += papplDevicePuts(device, command);
  bytes_written += papplDeviceWrite(device, &belen, 2);              // Big-endian length
  bytes_written += papplDeviceWrite(device, tpcl_job->comp_buffer, len);  // Compressed data
  bytes_written += papplDevicePuts(device, "|}\n");

  papplDeviceFlush(device);
  fprintf(stderr, "DEBUG: Device flushed\n");

  if (y)
    tpcl_job->comp_last_line = y;

  // Reset compression buffer
  memset(tpcl_job->comp_buffer, 0, 0xFFFF);
  tpcl_job->comp_buffer_ptr = tpcl_job->comp_buffer;
}
