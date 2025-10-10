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

#include <string.h>
#include <math.h>
#include "tpcl-driver.h"
#include "dithering.h"
//#include "tpcl-media.h" TODO remove


/*
 * TPCL Graphics Modes
 */

#define TEC_GMODE_TOPIX   1   // TOPIX compression (default, recommended)
#define TEC_GMODE_HEX_AND 3   // Raw hex AND mode
#define TEC_GMODE_HEX_OR  5   // Raw hex OR mode
//#define TEC_GMODE_TOPIX   3
//#define TEC_GMODE_HEX_AND 1
//#define TEC_GMODE_HEX_OR  5

/*
 * Job data structure
 */

typedef struct {
  unsigned char *buffer;           // Current line buffer
  unsigned char *last_buffer;      // Previous line buffer (for TOPIX)
  unsigned char *comp_buffer;      // Compression buffer
  unsigned char *comp_buffer_ptr;  // Current position in comp_buffer
  int           comp_last_line;    // Last line number sent
  int           page;              // Current page number
  int           gmode;             // Graphics mode (TOPIX/HEX)
  int           width;             // Line width in bytes
  int           height;            // Page height in lines
} tpcl_job_t;


/*
 * Driver information array
 */

const pappl_pr_driver_t tpcl_drivers[] = {
  {
    "toshiba-tec-tpcl",                          // name
    "Toshiba TEC TPCL v2 Label Printer",         // description
    "MFG:Toshiba Tec;MDL:TPCL;CMD:TPCL;",        // device_id
    NULL                                         // extension
  }
};

const int tpcl_drivers_count = sizeof(tpcl_drivers) / sizeof(tpcl_drivers[0]);


/*
 * Local functions
 */

static void tpcl_topix_compress(tpcl_job_t *job, int y);
static void tpcl_topix_output_buffer(pappl_device_t *device, tpcl_job_t *tpcl_job, int y);


/*
 * Raster printing callbacks.
 */

bool tpcl_driver_cb(
    pappl_system_t         *system,
    const char             *driver_name,
    const char             *device_uri,
    const char             *device_id,
    pappl_pr_driver_data_t *driver_data,
    ipp_t                  **driver_attrs,
    void                   *data);

bool tpcl_status_cb(
    pappl_printer_t *printer);

void tpcl_identify_cb(
    pappl_printer_t          *printer, 
    pappl_identify_actions_t actions, 
    const char               *message);

bool tpcl_print_cb(
    pappl_job_t *job, 
    pappl_pr_options_t *options, 
    pappl_device_t *device);

bool tpcl_rstartjob_cb(
    pappl_job_t        *job,
    pappl_pr_options_t *options,
    pappl_device_t     *device);

bool tpcl_rstartpage_cb(
    pappl_job_t        *job,
    pappl_pr_options_t *options,
    pappl_device_t     *device,
    unsigned           page);

bool tpcl_rwriteline_cb(
    pappl_job_t         *job,
    pappl_pr_options_t  *options,
    pappl_device_t      *device,
    unsigned            y,
    const unsigned char *line);

bool tpcl_rendpage_cb(
    pappl_job_t        *job,
    pappl_pr_options_t *options,
    pappl_device_t     *device,
    unsigned           page);

bool tpcl_rendjob_cb(
    pappl_job_t        *job,
    pappl_pr_options_t *options,
    pappl_device_t     *device);

const char* tpcl_testpage_cb(
    pappl_printer_t *printer,
    char *buffer,
    size_t bufsize);

void tpcl_delete_cb(
    pappl_printer_t *printer,
    pappl_pr_driver_data_t *data);


/*
 * 'tpcl_driver_cb()' - Main driver callback
 *
 * Configures the printer driver capabilities and callbacks.
 */

bool
tpcl_driver_cb(
    pappl_system_t         *system,          // System config
    const char             *driver_name,     // Driver name
    const char             *device_uri,      // Device URI
    const char             *device_id,       // IEEE-1284 device ID
    pappl_pr_driver_data_t *driver_data,     // Driver data
    ipp_t                  **driver_attrs,   // Driver attributes
    void                   *data)            // Context (not used)
{

  (void)data;

  // Costom driver extensions
  //driver_data->extension;                             // Extension data (managed by driver)

  // Set callbacks
  driver_data->status_cb     = tpcl_status_cb;          // Printer status callback. TODO: Implement
  driver_data->identify_cb   = tpcl_identify_cb;        // Identify-Printer callback (eg. make a sound). TODO: Implement
  driver_data->printfile_cb  = tpcl_print_cb;           // Print (raw) file callback. TODO: Implement
  driver_data->rstartjob_cb  = tpcl_rstartjob_cb;       // Start raster job callback
  driver_data->rstartpage_cb = tpcl_rstartpage_cb;      // Start raster page callback
  driver_data->rwriteline_cb = tpcl_rwriteline_cb;      // Write raster line callback
  driver_data->rendpage_cb   = tpcl_rendpage_cb;        // End raster page callback
  driver_data->rendjob_cb    = tpcl_rendjob_cb;         // End raster job callback
  driver_data->testpage_cb   = tpcl_testpage_cb;        // Test page print callback. TODO: Implement
  driver_data->delete_cb     = tpcl_delete_cb;          // Printer deletion callback. TODO: Implement

  // Model-agnostic printer options
  dither_threshold16(driver_data->gdither, 128);        // dithering for 'auto', 'text', and 'graphic' TODO let user choose cutoff
  dither_bayer16(driver_data->pdither);                 // dithering for 'photo'
  driver_data->format = "application/vnd.toshiba-tpcl"; // Native file format
  driver_data->ppm = 10;                                // Pages per minute (guesstimate)
  driver_data->ppm_color = 0;                           // No color printing
  //pappl_icon_t icons[3];                              // "printer-icons" values. TODO: Implement
  driver_data->kind = PAPPL_KIND_LABEL;		            	// "printer-kind" values
  driver_data->has_supplies = false;                    // Printer can report supplies. TODO: Implement
  driver_data->input_face_up = true;		                // Does input media come in face-up?
  driver_data->output_face_up = true;		                // Does output media come out face-up?
  driver_data->orient_default = IPP_ORIENT_NONE;        // "orientation-requested-default" value
  driver_data->color_supported = PAPPL_COLOR_MODE_MONOCHROME;   // Highest supported color mode
  driver_data->color_default = PAPPL_COLOR_MODE_MONOCHROME;     // Default color mode
  driver_data->content_default = PAPPL_CONTENT_AUTO;    // "print-content-default" value
  driver_data->quality_default = IPP_QUALITY_NORMAL;    // "print-quality-default" value
  driver_data->scaling_default = PAPPL_SCALING_AUTO;    // "print-scaling-default" value
  driver_data->raster_types = PAPPL_PWG_RASTER_TYPE_BLACK_1 |   // IPP supported color schemes / raster types TODO check if correct, missing grayscale
                              PAPPL_PWG_RASTER_TYPE_BLACK_8;
  driver_data->force_raster_type = PAPPL_PWG_RASTER_TYPE_NONE;  // Force a particular raster type?
  driver_data->duplex = PAPPL_DUPLEX_NONE;              // Duplex printing modes supported
  driver_data->sides_supported = PAPPL_SIDES_ONE_SIDED; // "sides-supported" values
  driver_data->sides_default = PAPPL_SIDES_ONE_SIDED;   // "sides-default" value
  driver_data->borderless = true;                       // Borderless margins supported?
  driver_data->left_right = 0;                          // Left and right margins in hundredths of millimeters
  driver_data->bottom_top = 0;                          // Bottom and top margins in hundredths of millimeters	

  // Model-specific printer options
  if (true) {                                           // TODO actually determine the printer
    strncpy(driver_data->make_and_model, "Toshiba TEC TPCL v2 Label Printer", // TODO use more specific name for each printer
      sizeof(driver_data->make_and_model) - 1);
    driver_data->make_and_model[sizeof(driver_data->make_and_model) - 1] = '\0';
    driver_data->finishings = PAPPL_FINISHINGS_NONE; // "finishings-supported" values

    // Supported resolutions (203dpi = 8 dots/mm, 300dpi = 12 dots/mm, 600dpi = 24 dots/mm)
    driver_data->num_resolution = 2;                 // Number of printer resolutions
    driver_data->x_resolution[0] = 203;              // Horizontal printer resolutions
    driver_data->x_resolution[1] = 300;
    driver_data->y_resolution[0] = 203;              // Vertical printer resolutions
    driver_data->y_resolution[1] = 300;
    driver_data->x_default = 203;                    // Default horizontal resolution
    driver_data->y_default = 203;                    // Default vertical resolution

    // Common label sizes - using standard PWG media names - TODO clean and move to roll definitions, which currently does not work as PAPPL rejects the print job
    // Use roll media for label printers - allows any size within range
    // Range: 6x6mm (min) to 203x330mm (max)
    //driver_data->num_media = 2;
    //driver_data->media[0] = "roll_min_6x6mm";
    //driver_data->media[1] = "roll_max_203x330mm";
    //WORKAROUND We define a selection of common sizes instead of roll_min/roll_max
    // due to validation in PAPPL 1.4.9

    // Supported media
    int media_idx = 0; // TODO this could lead to an overflow as it ignores PAPPL_MAX_MEDIA
    driver_data->media[media_idx++] = "oe_103x199mm_4.055x7.835in";     // DHL label
    driver_data->num_media = media_idx;              // Number of supported media

    // Available media sources
    media_idx = 0;  // TODO this could lead to an overflow as it ignores PAPPL_MAX_SOURCE
    driver_data->source[0] = "main-roll";            // Media sources
	  driver_data->num_source = media_idx;             // Number of media sources (trays/rolls)

    // Available media types
    media_idx = 0;  // TODO this could lead to an overflow as it ignores PAPPL_MAX_TYPE
    driver_data->type[0] = "labels";                 // Media types
    driver_data->type[1] = "labels-continuous";
    driver_data->type[2] = "direct-thermal";
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
      papplCopyString(driver_data->media_ready[i].size_name, "oe_103x199mm_4.055x7.835in", sizeof(driver_data->media_ready[i].size_name)); // Media name

      papplCopyString(driver_data->media_ready[i].source, driver_data->source[i], sizeof(driver_data->media_ready[i].source)); // PWG media source name
      driver_data->media_ready[i].top_margin    = driver_data->bottom_top; // Top margin in hundredths of millimeters
      driver_data->media_ready[i].top_offset    = 0;                       // Top offset in hundredths of millimeters
      driver_data->media_ready[i].tracking      = 0;                       // IPP media tracking type, we have no tracking

      papplCopyString(driver_data->media_ready[i].type, driver_data->type[0],  sizeof(driver_data->media_ready[i].type)); // PWG media type name
    }

    driver_data->media_default = driver_data->media_ready[0]; // Default media
  }

  driver_data->left_offset_supported[0] = 0;         // media-left-offset-supported (0,0 for none)
  driver_data->left_offset_supported[1] = 0;
  driver_data->top_offset_supported[0] = 0;          // media-top-offset-supported (0,0 for none)
  driver_data->top_offset_supported[1] = 0;
  driver_data->num_bin = 0;                          // Number of output bins
  //driver_data->*bin[PAPPL_MAX_BIN];                // Output bins
  //driver_data->bin_default;                        // Default output bin
  driver_data->mode_configured = 0;                  // label-mode-configured TODO set coorect flag
  driver_data->mode_supported = 0;                   // label-mode-supported TODO set correct flag
  driver_data->tear_offset_configured = 0;           // label-tear-offset-configured TODO set correct flag
  driver_data->tear_offset_supported[0] = 0;         // label-tear-offset-supported (0,0 for none) TODO set via user
  driver_data->tear_offset_supported[1] = 0;
  driver_data->speed_supported[0] = 2;               // print-speed-supported (in inches per second?) (0,0 for none) TODO check value correctness
  driver_data->speed_supported[1] = 10;
  driver_data->speed_default = 3;                    // print-speed-default TODO check value correctness

  // TODO 
  //driver_data->darkness_default;                   // print-darkness-default
  //driver_data->darkness_configured;                // printer-darkness-configured
  //driver_data->darkness_supported;                 // printer/print-darkness-supported (0 for none)
  //driver_data->identify_default;                   // "identify-actions-default" values
  //driver_data->identify_supported;                 // "identify-actions-supported" values
  //driver_data->num_features;                       // Number of "ipp-features-supported" values
  //driver_data->*features[PAPPL_MAX_VENDOR];        // "ipp-features-supported" values
  //driver_data->num_vendor;                         // Number of vendor attributes
  //driver_data->*vendor[PAPPL_MAX_VENDOR];          // Vendor attribute names

  return true;
}


/*
 * 'tpcl_status_cb()' - Get printer status
 *
 * Currently not implemented, just returns false. TODO
 */

bool
tpcl_status_cb(
    pappl_printer_t *printer)
{
  printf("Printer status check currently not implemented!");
  return true;
}


/*
 * 'tpcl_identify_cb()' - Identify printer
 *
 * Currently not implemented. TODO
 */

void tpcl_identify_cb(
    pappl_printer_t          *printer, 
    pappl_identify_actions_t actions, 
    const char               *message)
{
  printf("Printer identification currently not implemented!");
  return;
}


/*
 * 'tpcl_print_cb()' - Print raw file callback
 *
 * Currently not implemented, just returns false. TODO
 */

bool
tpcl_print_cb(
    pappl_job_t *job, 
    pappl_pr_options_t *options, 
    pappl_device_t *device)
{
  printf("Raw file printing currently not implemented!");
  return false;
}


/*
 * 'tpcl_rstartjob_cb()' - Start a print job
 */

bool
tpcl_rstartjob_cb(
    pappl_job_t        *job,
    pappl_pr_options_t *options,
    pappl_device_t     *device)
{
  printf(" Starting print job");

  tpcl_job_t *tpcl_job;

  // Allocate job data structure
  tpcl_job = (tpcl_job_t *)calloc(1, sizeof(tpcl_job_t));
  if (!tpcl_job) return false;

  papplJobSetData(job, tpcl_job);

  // Send reset command to clear any previous state
  papplDevicePuts(device, "{WS|}\n");

  // Send default setup commands
  // TODO: These should be configurable via job options
  papplDevicePuts(device, "{AX;+00,+00,+00|}\n");  // Feed adjustments (feed, cut, back)
  papplDevicePuts(device, "{RM;0,0|}\n");          // Ribbon motor (forward, back)

  return true;
}


/*
 * 'tpcl_rstartpage_cb()' - Start a page
 */

bool
tpcl_rstartpage_cb(
    pappl_job_t        *job,
    pappl_pr_options_t *options,
    pappl_device_t     *device,
    unsigned           page)
{
  printf(" Starting new page");

  tpcl_job_t *tpcl_job = (tpcl_job_t *)papplJobGetData(job);
  char       command[256];
  int        length, width, labelpitch, labelgap;

  if (!tpcl_job) return false;

  tpcl_job->page = page;
  tpcl_job->width = options->header.cupsBytesPerLine;
  tpcl_job->height = options->header.cupsHeight;

  // Calculate label dimensions in 0.1mm units (TPCL format)
  // cupsPageSize is in points (1/72 inch), convert to 0.1mm: points * 254 / 72
  width = (int)(options->header.cupsPageSize[0] * 254.0 / 72.0);
  length = (int)(options->header.cupsPageSize[1] * 254.0 / 72.0);
  labelgap = 30;  // 3mm default gap between labels
  labelpitch = length + labelgap;

  // Send label size command: {Dlabelpitch,width,length,peeloff_position|}
  snprintf(command, sizeof(command),
           "{D%04d,%04d,%04d,%04d|}\n",
           labelpitch, width, length, width + labelgap);
  papplDevicePuts(device, command);

  // Send temperature adjustment command: {AY;temp_adjust,ribbon_mode|}
  // temp_adjust: +00 = normal, ribbon_mode: 0 = thermal transfer, 1 = direct thermal
  papplDevicePuts(device, "{AY;+00,0|}\n");

  // Clear image buffer
  papplDevicePuts(device, "{C|}\n");

  // Determine graphics mode (default to TOPIX for best compression)
  tpcl_job->gmode = TEC_GMODE_TOPIX;

  if (tpcl_job->gmode == TEC_GMODE_TOPIX)
  {
    // Allocate buffers for TOPIX compression
    tpcl_job->buffer = malloc(tpcl_job->width);
    tpcl_job->last_buffer = malloc(tpcl_job->width);
    tpcl_job->comp_buffer = malloc(0xFFFF);

    if (!tpcl_job->buffer || !tpcl_job->last_buffer || !tpcl_job->comp_buffer)
    {
      if (tpcl_job->buffer) free(tpcl_job->buffer);
      if (tpcl_job->last_buffer) free(tpcl_job->last_buffer);
      if (tpcl_job->comp_buffer) free(tpcl_job->comp_buffer);
      return false;
    }

    memset(tpcl_job->last_buffer, 0, tpcl_job->width);
    memset(tpcl_job->comp_buffer, 0, 0xFFFF);

    tpcl_job->comp_buffer_ptr = tpcl_job->comp_buffer;
    tpcl_job->comp_last_line = 0;
  }
  else
  {
    // Allocate buffer for hex mode
    tpcl_job->buffer = malloc(tpcl_job->width);
    if (!tpcl_job->buffer)
      return false;

    // Send graphics header for hex mode: {SG;x,y,width,height,mode,data|}
    snprintf(command, sizeof(command),
             "{SG;0000,0000,%04d,%04d,%d,",
             tpcl_job->width * 8, tpcl_job->height, tpcl_job->gmode);
    papplDevicePuts(device, command);
  }

  return true;
}


/*
 * 'tpcl_rwriteline_cb()' - Write a line of raster data
 */

bool
tpcl_rwriteline_cb(
    pappl_job_t         *job,
    pappl_pr_options_t  *options,
    pappl_device_t      *device,
    unsigned            y,
    const unsigned char *line)
{
  tpcl_job_t *tpcl_job = (tpcl_job_t *)papplJobGetData(job);

  if (!tpcl_job || !tpcl_job->buffer)
    return false;

  // Copy line to buffer
  memcpy(tpcl_job->buffer, line, tpcl_job->width);

  if (tpcl_job->gmode == TEC_GMODE_TOPIX)
  {
    // Compress using TOPIX algorithm
    tpcl_topix_compress(tpcl_job, y);

    // Check if compression buffer is getting full, flush if needed
    if ((tpcl_job->comp_buffer_ptr - tpcl_job->comp_buffer) >
        (0xFFFF - (tpcl_job->width + ((tpcl_job->width / 8) * 3))))
    {
      tpcl_topix_output_buffer(device, tpcl_job, y);
      memset(tpcl_job->last_buffer, 0, tpcl_job->width);
    }
  }
  else
  {
    // Output raw hex data
    papplDeviceWrite(device, tpcl_job->buffer, tpcl_job->width);
  }

  return true;
}


/*
 * 'tpcl_rendpage()' - End a page
 */

bool
tpcl_rendpage_cb(
    pappl_job_t        *job,
    pappl_pr_options_t *options,
    pappl_device_t     *device,
    unsigned           page)
{
  printf(" Ending current page");

  tpcl_job_t *tpcl_job = (tpcl_job_t *)papplJobGetData(job);
  char       command[256];

  if (!tpcl_job) return false;

  // Flush any remaining TOPIX data or close hex graphics
  if (tpcl_job->gmode == TEC_GMODE_TOPIX)
  {
    tpcl_topix_output_buffer(device, tpcl_job, 0);
  }
  else
  {
    papplDevicePuts(device, "|}\n");
  }

  // Send print command: {XS;I,copies,cut+detect+mode+speed+media+mirror+status|}
  // Format: cut(3 digits) + detect(1) + mode(1) + speed(1) + media(1) + mirror(1) + status(1)
  // Simplified: copies=1, cut=0, detect=0, mode=C (tear-off), speed=3, media=1 (thermal), mirror=0, status=0
  snprintf(command, sizeof(command),
           "{XS;I,%04d,000%d%c%d%d%d%d|}\n",
           options->copies,
           0,    // detect (media tracking) - TODO: make configurable
           'C',  // mode: C=tear-off, D=peel-off, E=rewind
           3,    // speed
           1,    // media: 0=direct thermal, 1=thermal transfer, 2=ribbon saving
           0,    // mirror
           0);   // status
  papplDevicePuts(device, command);

  // Send padding to avoid zero window error (TCP workaround)
  papplDevicePrintf(device, "%1024s", "");

  // Free page buffers
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

  return true;
}


/*
 * 'tpcl_rendjob_cb()' - End a job
 */

bool
tpcl_rendjob_cb(
    pappl_job_t        *job,
    pappl_pr_options_t *options,
    pappl_device_t     *device)
{
  printf(" Ending current job");

  tpcl_job_t *tpcl_job = (tpcl_job_t *)papplJobGetData(job);

  // Free job data structure
  if (tpcl_job)
  {
    free(tpcl_job);
    papplJobSetData(job, NULL);
  }

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
    tpcl_job_t *job,
    int        y)
{
  int           i, l1, l2, l3, max, width;
  unsigned char line[8][9][9] = {{{0}}};
  unsigned char cl1, cl2, cl3, xor;
  unsigned char *ptr;

  width = job->width;
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
        xor = job->buffer[i] ^ job->last_buffer[i];
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
  *job->comp_buffer_ptr = cl1;
  job->comp_buffer_ptr++;

  // Copy only non-zero bytes to compressed buffer
  if (cl1 > 0)
  {
    ptr = &line[0][0][0];
    for (i = 0; i < max; i++)
    {
      if (*ptr != 0)
      {
        *job->comp_buffer_ptr = *ptr;
        job->comp_buffer_ptr++;
      }
      ptr++;
    }
  }

  // Copy current line to last_buffer for next iteration
  memcpy(job->last_buffer, job->buffer, width);
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
  if (len == 0)
    return;

  // Convert length to big-endian (network byte order)
  belen = (len << 8) | (len >> 8);

  // Send SG command with compressed data
  // Format: {SG;x,y,width,height,mode,length+data|}
  snprintf(command, sizeof(command),
           "{SG;0000,%04d,%04d,0300,%d,",
           tpcl_job->comp_last_line,
           tpcl_job->width * 8,
           tpcl_job->gmode);

  papplDevicePuts(device, command);
  papplDeviceWrite(device, &belen, 2);              // Big-endian length
  papplDeviceWrite(device, tpcl_job->comp_buffer, len);  // Compressed data
  papplDevicePuts(device, "|}\n");

  if (y)
    tpcl_job->comp_last_line = y;

  // Reset compression buffer
  memset(tpcl_job->comp_buffer, 0, 0xFFFF);
  tpcl_job->comp_buffer_ptr = tpcl_job->comp_buffer;
}
