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

#include <string.h>
#include <math.h>
#include "tpcl-driver.h"
#include "tpcl-media.h"


/*
 * TPCL Graphics Modes
 */

#define TEC_GMODE_TOPIX   1   // TOPIX compression (default, recommended)
#define TEC_GMODE_HEX_AND 3   // Raw hex AND mode
#define TEC_GMODE_HEX_OR  5   // Raw hex OR mode


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
    "toshiba-tec-tpcl",                           // name
    "Toshiba TEC TPCL v2 Label Printer",         // description
    "MFG:Toshiba Tec;MDL:TPCL;CMD:TPCL;",        // device_id
    NULL                                          // extension
  }
};


/*
 * Local functions
 */

static void tpcl_topix_compress(tpcl_job_t *job, int y);
static void tpcl_topix_output_buffer(pappl_device_t *device, tpcl_job_t *tpcl_job, int y);


/*
 * 'tpcl_driver_cb()' - Main driver callback
 *
 * Configures the printer driver capabilities and callbacks.
 */

bool
tpcl_driver_cb(
    pappl_system_t         *system,
    const char             *driver_name,
    const char             *device_uri,
    const char             *device_id,
    pappl_pr_driver_data_t *driver_data,
    ipp_t                  **driver_attrs,
    void                   *data)
{
  int i;

  // Set basic driver information
  strncpy(driver_data->make_and_model, "Toshiba TEC TPCL v2 Label Printer",
          sizeof(driver_data->make_and_model) - 1);
  driver_data->make_and_model[sizeof(driver_data->make_and_model) - 1] = '\0';

  // Supported resolutions (203dpi = 8 dots/mm, 300dpi = 12 dots/mm, 600dpi = 24 dots/mm)
  driver_data->num_resolution = 3;
  driver_data->x_resolution[0] = 203;
  driver_data->y_resolution[0] = 203;
  driver_data->x_resolution[1] = 300;
  driver_data->y_resolution[1] = 300;
  driver_data->x_resolution[2] = 600;
  driver_data->y_resolution[2] = 600;
  driver_data->x_default = driver_data->y_default = 203;

  // Color support - monochrome only
  driver_data->color_supported = PAPPL_COLOR_MODE_MONOCHROME;
  driver_data->color_default = PAPPL_COLOR_MODE_MONOCHROME;

  // Raster types supported
  driver_data->raster_types = PAPPL_PWG_RASTER_TYPE_BLACK_1 |
                              PAPPL_PWG_RASTER_TYPE_BLACK_8;

  // Add media sizes from tpcl_media_sizes array
  driver_data->num_media = 0;
  for (i = 0; tpcl_media_sizes[i].pwg_name != NULL &&
              driver_data->num_media < PAPPL_MAX_MEDIA; i++)
  {
    driver_data->media[driver_data->num_media] = tpcl_media_sizes[i].pwg_name;
    driver_data->num_media++;
  }

  // Default media size (4" x 5" is common for labels)
  // media_default is a pappl_media_col_t structure
  strncpy(driver_data->media_default.size_name, "w288h360",
          sizeof(driver_data->media_default.size_name) - 1);
  driver_data->media_default.size_width = 288 * 2540 / 72;  // Convert points to hundredths of mm
  driver_data->media_default.size_length = 360 * 2540 / 72;

  // Media sources (single roll/tray)
  driver_data->num_source = 1;
  driver_data->source[0] = "main-roll";

  // Media types
  driver_data->num_type = 3;
  driver_data->type[0] = "labels";
  driver_data->type[1] = "labels-continuous";
  driver_data->type[2] = "direct-thermal";

  // Print speeds (inches per second)
  driver_data->speed_supported[0] = 2;
  driver_data->speed_supported[1] = 10;
  driver_data->speed_default = 3;

  // Margins (label printers have no margins)
  driver_data->left_right = 0;
  driver_data->bottom_top = 0;

  // Duplex not supported
  driver_data->duplex = PAPPL_DUPLEX_NONE;

  // Finishings (cutter support)
  driver_data->finishings = PAPPL_FINISHINGS_TRIM;

  // Set raster callbacks
  driver_data->printfile_cb = NULL;           // Use raster callbacks
  driver_data->rendjob_cb = tpcl_rendjob;
  driver_data->rendpage_cb = tpcl_rendpage;
  driver_data->rstartjob_cb = tpcl_rstartjob;
  driver_data->rstartpage_cb = tpcl_rstartpage;
  driver_data->rwriteline_cb = tpcl_rwriteline;
  driver_data->status_cb = tpcl_status;

  return true;
}


/*
 * 'tpcl_rstartjob()' - Start a print job
 */

bool
tpcl_rstartjob(
    pappl_job_t        *job,
    pappl_pr_options_t *options,
    pappl_device_t     *device)
{
  tpcl_job_t *tpcl_job;

  // Allocate job data structure
  tpcl_job = (tpcl_job_t *)calloc(1, sizeof(tpcl_job_t));
  if (!tpcl_job)
    return false;

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
 * 'tpcl_rstartpage()' - Start a page
 */

bool
tpcl_rstartpage(
    pappl_job_t        *job,
    pappl_pr_options_t *options,
    pappl_device_t     *device,
    unsigned           page)
{
  tpcl_job_t *tpcl_job = (tpcl_job_t *)papplJobGetData(job);
  char       command[256];
  int        length, width, labelpitch, labelgap;

  if (!tpcl_job)
    return false;

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
 * 'tpcl_rwriteline()' - Write a line of raster data
 */

bool
tpcl_rwriteline(
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
tpcl_rendpage(
    pappl_job_t        *job,
    pappl_pr_options_t *options,
    pappl_device_t     *device,
    unsigned           page)
{
  tpcl_job_t *tpcl_job = (tpcl_job_t *)papplJobGetData(job);
  char       command[256];

  if (!tpcl_job)
    return false;

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
 * 'tpcl_rendjob()' - End a job
 */

bool
tpcl_rendjob(
    pappl_job_t        *job,
    pappl_pr_options_t *options,
    pappl_device_t     *device)
{
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
 * 'tpcl_status()' - Get printer status
 */

bool
tpcl_status(
    pappl_printer_t *printer)
{
  // TODO: Query actual printer status via TPCL commands
  // For now, just return true (printer is OK)
  return true;
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
  unsigned char line[8][9][9] = {{0}};
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
