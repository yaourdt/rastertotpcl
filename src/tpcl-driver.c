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
#include "dithering.h"
#include "icon-48.h"
#include "icon-128.h"
#include "icon-512.h"
#include <math.h>
#include <sys/stat.h>
#include <errno.h>

/*
 * Constants for conversion
 */

#define POINTS_PER_INCH 72.0
#define MM_PER_INCH     25.4


/*
 * Driver information array
 * Name, description, IEEE-1284 device ID, extension
 */

const pappl_pr_driver_t tpcl_drivers[] = {
  {"B-SA4G",      "Tec B-SA4G",      "CMD:TPCL",             NULL},
  {"B-SA4T",      "Tec B-SA4T",      "CMD:TPCL",             NULL},
  {"B-SX4",       "Tec B-SX4",       "CMD:TPCL",             NULL},
  {"B-SX5",       "Tec B-SX5",       "CMD:TPCL",             NULL},
  {"B-SX6",       "Tec B-SX6",       "CMD:TPCL",             NULL},
  {"B-SX8",       "Tec B-SX8",       "CMD:TPCL",             NULL},
  {"B-482",       "Tec B-482",       "CMD:TPCL",             NULL},
  {"B-572",       "Tec B-572",       "CMD:TPCL",             NULL},
  {"B-852R",      "Tec B-852R",      "CMD:TPCL",             NULL},
  {"B-SV4D",      "Tec B-SV4D",      "CMD:TPCL",             NULL},
  {"B-SV4T",      "Tec B-SV4T",      "CMD:TPCL",             NULL},
  {"B-EV4D-GS14", "Tec B-EV4D-GS14", "CMD:TPCL;MDL:B-EV4-G", NULL},
  {"B-EV4T-GS14", "Tec B-EV4T-GS14", "CMD:TPCL;MDL:B-EV4-G", NULL}
};

const int tpcl_drivers_count = sizeof(tpcl_drivers) / sizeof(tpcl_drivers[0]);


/*
 * Printer information array, extends information from 'tpcl_drivers[]'
 */

#define TPCL_PRNT_SPEED 3

typedef struct {
    const char             *name;                        // Name, equal to name in 'tpcl_drivers[]', only used for human reference
    int                    print_min_width;              // Minimum label width in x direction in points (1 point = 1/72 inch)
    int                    print_min_height;             // Minimum label length in y direction in points
    int                    print_max_width;              // Maximum label width in x direction in points
    int                    print_max_height;             // Maximum label length in y direction in points
    bool                   resolution_203;               // Printer offers 203 dpi resolution if true
    bool                   resolution_300;               // Printer offers 300 dpi resolution if true
    bool                   thermal_transfer;             // Direct thermal media are alyways supported, if true also thermal transfer media are allowed
    bool                   thermal_transfer_with_ribbon; // Thermal transfer with ribbon support if true
    int                    print_speeds[TPCL_PRNT_SPEED];// Print speed settings as Toshiba enum (min, default, max)
} tpcl_printer_t;

const tpcl_printer_t tpcl_printer_properties[] = {
  {"B-SA4G",        63,   29,  300, 2830, true,  false, true,  false, {0x2, 0x4, 0x6}},
  {"B-SA4T",        63,   29,  300, 2830, false, true , true,  false, {0x2, 0x4, 0x6}},
  {"B-SX4",         72,   23,  295, 4246, true,  false, false, true,  {0x3, 0x6, 0xA}},
  {"B-SX5",         73,   29,  362, 4246, true,  true , false, true,  {0x3, 0x5, 0x8}},
  {"B-SX6",        238,   29,  483, 4246, true,  true , false, true,  {0x3, 0x4, 0x8}},
  {"B-SX8",        286,   29,  605, 4246, true,  true , false, true,  {0x3, 0x4, 0x8}},
  {"B-482",         72,   23,  295, 4246, true,  true , false, true,  {0x3, 0x5, 0x8}},
  {"B-572",         73,   29,  362, 4246, true,  true , false, true,  {0x3, 0x5, 0x8}},
  {"B-852R",       283,   35,  614, 1814, false, true , false, false, {0x2, 0x4, 0x8}},
  {"B-SV4D",        71,   23,  306, 1726, true,  false, false, false, {0x2, 0x3, 0x5}},
  {"B-SV4T",        71,   23,  306, 1726, true,  false, true,  false, {0x2, 0x3, 0x5}},
  {"B-EV4D-GS14",   71,   23,  306, 1726, true,  true , false, false, {0x2, 0x3, 0x5}},
  {"B-EV4T-GS14",   71,   23,  306, 1726, true,  true , true,  false, {0x2, 0x3, 0x5}}
};

/*
 * Job data structure
 */

typedef struct {
  int                      gmode;                        // Graphics mode (TOPIX, hex, nibble)
  int                      print_width;                  // Effective print width (0.1mm)
  int                      print_height;                 // Effective print height (0.1mm)
  int                      label_pitch;                  // Label pitch = print height + label gap
  int                      roll_width;                   // Roll width
  unsigned int             buffer_len;                   // Length of line buffer as sent to printer
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

static int tpcl_get_int_option(
  ipp_t                    *attrs,
  const char               *name,
  int                      default_val,
  pappl_job_t              *job,
  pappl_printer_t          *printer
);

static const char* tpcl_get_str_option(
  ipp_t                    *attrs,
  const char               *name,
  const char               *default_val,
  pappl_job_t              *job,
  pappl_printer_t          *printer
);

static void tpcl_add_vendor_int_option(
  pappl_pr_driver_data_t   *driver_data,
  ipp_t                    **driver_attrs,
  const char               *name,
  int                      min,
  int                      max,
  int                      default_val
);

static void tpcl_add_vendor_str_option(
  pappl_pr_driver_data_t   *driver_data,
  ipp_t                    **driver_attrs,
  const char               *name,
  int                      num_values,
  const char               **values,
  const char               *default_val
);

static bool tpcl_get_label_dimensions(
  ipp_t                    *printer_attrs,
  int                      print_width,
  int                      print_height,
  int                      *label_pitch,
  int                      *roll_width,
  pappl_job_t              *job,
  pappl_printer_t          *printer
);

static bool tpcl_get_feed_adjustments(
  ipp_t                    *printer_attrs,
  int                      *feed_adjustment,
  int                      *cut_position_adjustment,
  int                      *backfeed_adjustment,
  pappl_job_t              *job,
  pappl_printer_t          *printer
);

static char tpcl_map_sensor_type(
  const char               *sensor_type
);

static char tpcl_map_cut_type(
  const char               *cut_type
);

static char tpcl_map_feed_mode(
  const char               *feed_mode
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

  //
  // Define Toshiba TEC specific vendor options (max 32 allowed by PAPPL)
  //
  driver_data->num_vendor = 0;                           // Number of available vendor options
  if (!*driver_attrs) *driver_attrs = ippNew();          // Create IPP attributes to describe above vendor options

  // Gap between labels in units of 0.1mm
  tpcl_add_vendor_int_option(driver_data, driver_attrs, "label-gap", 0, 200, 50);

  // Roll margin in units of 0.1mm (width difference between backing paper and label)
  tpcl_add_vendor_int_option(driver_data, driver_attrs, "roll-margin", 0, 300, 10);

  // Sensor type for label detection
  tpcl_add_vendor_str_option(driver_data, driver_attrs, "sensor-type", 5, (const char *[]){"none", "reflective", "transmissive", "reflective-pre-print", "transmissive-pre-print"}, "transmissive");

  // Cut/non-cut selection
  tpcl_add_vendor_str_option(driver_data, driver_attrs, "label-cut", 2, (const char *[]){"non-cut", "cut"}, "non-cut");

  // Cut interval (number of labels before cutting, 0=no cut, 1-100)
  tpcl_add_vendor_int_option(driver_data, driver_attrs, "cut-interval", 0, 100, 0);

  // Feed mode selection
  tpcl_add_vendor_str_option(driver_data, driver_attrs, "feed-mode", 4, (const char *[]){"batch", "strip-backfeed-sensor", "strip-backfeed-no-sensor", "partial-cut"}, "batch");

  // Feed on label size change?
  tpcl_add_vendor_str_option(driver_data, driver_attrs, "feed-on-label-size-change", 2, (const char *[]){"yes", "no"}, "yes");

  // Graphics mode selection
  tpcl_add_vendor_str_option(driver_data, driver_attrs, "graphics-mode", 5, (const char *[]){"nibble-and", "hex-and", "topix", "nibble-or", "hex-or"}, "topix");

  // Dithering algorithm selection
  tpcl_add_vendor_str_option(driver_data, driver_attrs, "dithering-algorithm", 3, (const char *[]){"threshold", "bayer", "clustered"}, "threshold");

  // Dithering threshold level (0-255, only used with 'threshold' algorithm)
  tpcl_add_vendor_int_option(driver_data, driver_attrs, "dithering-threshold", 0, 255, 128);

  // Feed adjustment value (-500 to 500 in 0.1mm units, negative = forward, positive = backward, 0 = no adjustment)
  tpcl_add_vendor_int_option(driver_data, driver_attrs, "feed-adjustment", -500, 500, 0);

  // Cut position adjustment value (-180 to 180 in 0.1mm units, negative = forward, positive = backward, 0 = no adjustment)
  tpcl_add_vendor_int_option(driver_data, driver_attrs, "cut-position-adjustment", -180, 180, 0);

  // Backfeed adjustment value (-99 to 99 in 0.1mm units, negative = decrease, positive = increase, 0 = no adjustment)
  tpcl_add_vendor_int_option(driver_data, driver_attrs, "backfeed-adjustment", -99, 99, 0);

  //
  // Model-agnostic printer options
  //

  // Configure dithering based on default IPP attributes
  const char *dither_algo = tpcl_get_str_option(*driver_attrs, "dithering-algorithm", "threshold", NULL, NULL);

  if (strcmp(dither_algo, "bayer") == 0)
  {
    dither_bayer16(driver_data->gdither);                // Dithering for 'auto', 'text', and 'graphic'
    dither_bayer16(driver_data->pdither);                // Dithering for 'photo' (currently not supported)
  }
  else if (strcmp(dither_algo, "clustered") == 0)
  {
    dither_clustered16(driver_data->gdither);
    dither_clustered16(driver_data->pdither);
  }
  else
  {
    int dither_threshold = tpcl_get_int_option(*driver_attrs, "dithering-threshold", 128, NULL, NULL);
    dither_threshold16(driver_data->gdither, (unsigned char)dither_threshold);
    dither_threshold16(driver_data->pdither, (unsigned char)dither_threshold);
  }

  // Printer icons - 48x48, 128x128, and 512x512 pixel sizes (embedded)
  driver_data->icons[0].filename[0] = '\0';
  driver_data->icons[0].data = icon_48_png_data;
  driver_data->icons[0].datalen = icon_48_png_size;

  driver_data->icons[1].filename[0] = '\0';
  driver_data->icons[1].data = icon_128_png_data;
  driver_data->icons[1].datalen = icon_128_png_size;

  driver_data->icons[2].filename[0] = '\0';
  driver_data->icons[2].data = icon_512_png_data;
  driver_data->icons[2].datalen = icon_512_png_size;

  driver_data->format = "application/vnd.toshiba-tpcl";  // Native file format
  driver_data->ppm = 10;                                 // Pages per minute (guesstimate)
  driver_data->ppm_color = 0;                            // No color printing
  driver_data->kind = PAPPL_KIND_LABEL;		            	 // Type of printer
  driver_data->has_supplies = false;                     // Printer can report supplies.
  driver_data->input_face_up = true;		                 // Does input media come in face-up?
  driver_data->output_face_up = true;		                 // Does output media come out face-up?
  driver_data->orient_default = IPP_ORIENT_PORTRAIT;     // Default orientation
  driver_data->color_supported =                         // Highest supported color mode advertised via IPP
    PAPPL_COLOR_MODE_BI_LEVEL  |                         //  - black & white
    PAPPL_COLOR_MODE_MONOCHROME;                         //  - grayscale
  driver_data->color_default =                           // Default color mode 
    PAPPL_COLOR_MODE_BI_LEVEL;                           //  - black & white
  driver_data->content_default = PAPPL_CONTENT_AUTO;     // Optimize for vector graphics or image content
  driver_data->quality_default = IPP_QUALITY_NORMAL;     // Default print quality
  driver_data->scaling_default = PAPPL_SCALING_AUTO;     // Default print scaling
  driver_data->raster_types =                            // Supported color schemes by our driver callback
    PAPPL_PWG_RASTER_TYPE_BLACK_1 |                      // - black & white
    PAPPL_PWG_RASTER_TYPE_BLACK_8 |                      // - grayscale, 0xFF = black
    PAPPL_PWG_RASTER_TYPE_SGRAY_8;                       // - grayscale, 0xFF = white
  driver_data->force_raster_type =                       // Force a particular raster type? 
    PAPPL_PWG_RASTER_TYPE_NONE;
  driver_data->duplex = PAPPL_DUPLEX_NONE;               // Duplex printing modes supported
  driver_data->sides_supported = PAPPL_SIDES_ONE_SIDED;  // IPP "sides" bit values
  driver_data->sides_default = PAPPL_SIDES_ONE_SIDED;    // IPP "sides" bit values for default
  driver_data->finishings = PAPPL_FINISHINGS_NONE;       // Supported finishings such as punch or staple
  driver_data->num_bin = 0;                              // Number of output bins
  driver_data->identify_supported =                      // Supported identify actions (we actually feed a blank label)
    PAPPL_IDENTIFY_ACTIONS_SOUND;
  driver_data->identify_default =                        // Default identification action (we actually feed a blank label)
    PAPPL_IDENTIFY_ACTIONS_SOUND;
  driver_data->mode_supported =                          // Supported label processing modes
    PAPPL_LABEL_MODE_CUTTER |                            // - cut immediately after each label
    PAPPL_LABEL_MODE_CUTTER_DELAYED |                    // - cut after n labels
    PAPPL_LABEL_MODE_PEEL_OFF |                          // - directly stip the label so it is ready to be applied
    PAPPL_LABEL_MODE_TEAR_OFF;                           // - rewind label to tear bar position
  driver_data->mode_configured = 0;                      // Default label processing modes
  driver_data->tear_offset_supported[0] =   0;           // Min offset when in mode for tearing labels
  driver_data->tear_offset_supported[1] = 180;           // Max offset when in mode for tearing labels
  driver_data->tear_offset_configured = 0;               // Default offset when in mode for tearing labels

  //
  // Model-specific printer options
  //
  for (int i = 0; i < tpcl_drivers_count; i++)
  {
    if (!strcmp(driver_name, tpcl_drivers[i].name))
    {
      int x_mm, y_mm;
      char roll_min[PAPPL_MAX_MEDIA], roll_max[PAPPL_MAX_MEDIA];
      
      // Device name
      strncpy(driver_data->make_and_model, tpcl_drivers[i].description, 127);
      papplLog(system, PAPPL_LOGLEVEL_DEBUG, "Driver '%s' loaded from table position %d", tpcl_drivers[i].name, i);

      // Available printer resolutions
      driver_data->num_resolution = 0;                   // Number of printer resolutions
      if (tpcl_printer_properties[i].resolution_203)
      {
        driver_data->x_resolution[0] = 203;              // Horizontal printer resolutions
        driver_data->y_resolution[0] = 203;              // Vertical printer resolutions
        driver_data->x_default = 203;                    // Default horizontal resolution
        driver_data->y_default = 203;                    // Default vertical resolution
        driver_data->num_resolution++;
      }
      if (tpcl_printer_properties[i].resolution_300)
      {
        driver_data->x_resolution[driver_data->num_resolution] = 300;
        driver_data->y_resolution[driver_data->num_resolution] = 300;
        driver_data->x_default = 300;
        driver_data->y_default = 300;
        driver_data->num_resolution++;
      }
      papplLog(system, PAPPL_LOGLEVEL_DEBUG, "Resolution settings: num_resolution=%d, x_default=%d, y_default=%d", driver_data->num_resolution, driver_data->x_default, driver_data->y_default);
      if (driver_data->num_resolution == 0)
      {
        papplLog(system, PAPPL_LOGLEVEL_ERROR, "No resolution configured for driver '%s'", driver_name);
        return false;
      }
      
      // Available printing speeds (workaround due to PAPPL web interface limitations)
      driver_data->speed_supported[0] = 0;               // Min
      driver_data->speed_supported[1] = 0;               // Max
      driver_data->speed_default      = 0;               // Default
      tpcl_add_vendor_int_option(driver_data, driver_attrs, "print-speed", tpcl_printer_properties[i].print_speeds[0], tpcl_printer_properties[i].print_speeds[2], tpcl_printer_properties[i].print_speeds[1]);
      papplLog(system, PAPPL_LOGLEVEL_DEBUG, "Print speed settings: min=%d, default=%d, max=%d", tpcl_printer_properties[i].print_speeds[0], tpcl_printer_properties[i].print_speeds[1], tpcl_printer_properties[i].print_speeds[2]); 

      // Supported media (label) sizes. We use roll media for label printers, which allows any size within range
      driver_data->num_media = 2;                        // Number of supported media

      // Minimum label size
      x_mm = (int)ceil ( (tpcl_printer_properties[i].print_min_width  / POINTS_PER_INCH) * MM_PER_INCH );
      y_mm = (int)ceil ( (tpcl_printer_properties[i].print_min_height / POINTS_PER_INCH) * MM_PER_INCH );
      snprintf(roll_min, PAPPL_MAX_MEDIA, "roll_min_%dx%dmm", x_mm, y_mm);
      driver_data->media[0] = strdup(roll_min);

      // Maximum label size
      x_mm = (int)floor( (tpcl_printer_properties[i].print_max_width  / POINTS_PER_INCH) * MM_PER_INCH );
      y_mm = (int)floor( (tpcl_printer_properties[i].print_max_height / POINTS_PER_INCH) * MM_PER_INCH );
      snprintf(roll_max, PAPPL_MAX_MEDIA, "roll_max_%dx%dmm", x_mm, y_mm);
      driver_data->media[1] = strdup(roll_max);
      papplLog(system, PAPPL_LOGLEVEL_DEBUG, "Roll media dimensions: min=%s, max=%s", driver_data->media[0], driver_data->media[1]);

      // Available media sources
	    driver_data->num_source = 1;                       // Number of media sources (trays/rolls)
      driver_data->source[0] = "main-roll";              // Media sources
      papplCopyString(driver_data->media_ready[0].source, driver_data->source[0], 63);

      // Available media types
      driver_data->num_type = 1;                         // Number of media types
      driver_data->type[0] = "direct-thermal";           // Media types
      papplCopyString(driver_data->media_ready[0].type,   driver_data->type[0],   63);

      if (tpcl_printer_properties[i].thermal_transfer)
      {
        driver_data->type[driver_data->num_type] = "thermal-transfer";
        driver_data->num_type++;
      }

      if (tpcl_printer_properties[i].thermal_transfer_with_ribbon)
      {
        driver_data->type[driver_data->num_type] = "thermal-transfer-ribbon-saving";
        driver_data->num_type++;
        driver_data->type[driver_data->num_type] = "thermal-transfer-no-ribbon-saving";
        driver_data->num_type++;
      }
      papplLog(system, PAPPL_LOGLEVEL_DEBUG, "Media type settings: num_type=%d, thermal_transfer=%d, thermal_transfer_with_ribbon=%d", driver_data->num_type, tpcl_printer_properties[i].thermal_transfer, tpcl_printer_properties[i].thermal_transfer_with_ribbon);

      // Fill out ready media, by default we are not setting margins
      driver_data->borderless                   = false; // Offer the option to toggle borderless in the UI. Makes no sense, we are always borderless
      driver_data->left_right                   =     0; // Left and right margins in hundredths of millimeters
      driver_data->bottom_top                   =     0; // Bottom and top margins in hundredths of millimeters	
      driver_data->media_ready[0].top_margin    =     0; // Top margin in hundredths of millimeters
      driver_data->media_ready[0].bottom_margin =     0; // Bottom margin in hundredths of millimeters
      driver_data->media_ready[0].left_margin   =     0; // Left margin in hundredths of millimeters
      driver_data->media_ready[0].right_margin  =     0; // Right margin in hundredths of millimeters

      // Fill out ready media, we assume a default label of size 80x200mm to be loaded
      driver_data->media_ready[0].tracking      =     0; // IPP media tracking type, we have no tracking
      driver_data->media_ready[0].size_width    =  8000; // Width in hundredths of millimeters
      driver_data->media_ready[0].size_length   = 20000; // Height in hundredths of millimeters
      papplCopyString(driver_data->media_ready[0].size_name, "oe_toshiba_80x200mm", 63);
      papplLog(system, PAPPL_LOGLEVEL_DEBUG, "Media ready settings: size_name=%s, width=%d (0.01mm), length=%d (0.01mm)", driver_data->media_ready[0].size_name, driver_data->media_ready[0].size_width, driver_data->media_ready[0].size_length); 

      // Fill out ready media, by default there are no offsets
      driver_data->left_offset_supported[0]     =     0; // Minimum left printing offset supported
      driver_data->left_offset_supported[1]     =     0; // Maximum left printing offset supported
      driver_data->media_ready[0].left_offset   =     0; // Default left offset in hundredths of millimeters
      driver_data->top_offset_supported[0]      =     0; // Minimum top printing offset supported
      driver_data->top_offset_supported[1]      =     0; // Maximum top printing offset supported
      driver_data->media_ready[0].top_offset    =     0; // Default top offset in hundredths of millimeters

      // Printer darkness (workaround due to PAPPL web interface limitations)
      driver_data->darkness_supported           =     0; // Printer darkness
      driver_data->darkness_default             =     0; // Default darkness adjustment at printer power-on
      driver_data->darkness_configured          =     0; // Currently configured printing darkness
      tpcl_add_vendor_int_option(driver_data, driver_attrs, "print-darkness", -10, 10, 0);

      // Default media
      driver_data->media_default = driver_data->media_ready[0];
      break;
    }
  }

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

  // Build feed command dynamically {Tabcde|}
  // a: Sensor type (0=none, 1=reflective, 2=transmissive, 3=transmissive pre-print, 4=reflective pre-print)
  const char *sensor_type = tpcl_get_str_option(printer_attrs, "sensor-type", "transmissive", NULL, printer);
  char sensor_char = tpcl_map_sensor_type(sensor_type);

  // b: Cut selection (0=non-cut, 1=cut)
  const char *cut_type = tpcl_get_str_option(printer_attrs, "label-cut", "non-cut", NULL, printer);
  char cut_char = tpcl_map_cut_type(cut_type);

  // c: Feed mode (C=batch, D=strip with backfeed sensor valid, E=strip with backfeed sensor ignored, F=partial-cut)
  const char *feed_mode = tpcl_get_str_option(printer_attrs, "feed-mode", "batch", NULL, printer);
  char feed_mode_char = tpcl_map_feed_mode(feed_mode);

  // d: Feed speed (use default speed from driver data as hex char)
  char speed_char = '0' + driver_data.speed_default;
  if (driver_data.speed_default > 9)
    speed_char = 'A' + (driver_data.speed_default - 10);

  // e: Ribbon setting (0=direct thermal or thermal transfer without ribbon, 1=tt with ribbon saving, 2=tt without ribbon saving)
  char ribbon_char = '0';
  const char *media_type = driver_data.media_default.type;

  if (media_type)
  {
    if (strcmp(media_type, "thermal-transfer-ribbon-saving") == 0)
    {
      ribbon_char = '1';
    }
    else if (strcmp(media_type, "thermal-transfer-no-ribbon-saving") == 0)
    {
      ribbon_char = '2';
    }
  }

  snprintf(
    command,
    sizeof(command),
    "{T%c%c%c%c%c|}\n",
    sensor_char,
    cut_char,
    feed_mode_char,
    speed_char,
    ribbon_char
  );
  papplDevicePuts(device, command);
  papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Sending feed command: %s", command);

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
  tpcl_job->print_width  = (int)(options->header.cupsPageSize[0] * MM_PER_INCH * 10.0 / POINTS_PER_INCH);  // Effective print width (0.1mm)
  tpcl_job->print_height = (int)(options->header.cupsPageSize[1] * MM_PER_INCH * 10.0 / POINTS_PER_INCH);  // Effective print height (0.1mm)

  // Get label gap and roll margin from printer settings
  int label_gap = tpcl_get_int_option(printer_attrs, "label-gap", 50, job, NULL);
  int roll_margin = tpcl_get_int_option(printer_attrs, "roll-margin", 10, job, NULL);

  // Calculate label pitch and roll width from retrieved values
  tpcl_job->label_pitch = tpcl_job->print_height + label_gap;
  tpcl_job->roll_width = tpcl_job->print_width + roll_margin;

  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Calculated label dimensions from page size: width=%d (0.1mm), height=%d (0.1mm), pitch=%d (0.1mm), roll=%d (0.1mm)", tpcl_job->print_width, tpcl_job->print_height, tpcl_job->label_pitch, tpcl_job->roll_width);
  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Maximum image resolution at %ux%udpi: width=%u dots, height=%u dots", options->header.HWResolution[0], options->header.HWResolution[1], (unsigned int) (options->header.HWResolution[0] * options->header.cupsPageSize[0] / POINTS_PER_INCH), (unsigned int) (options->header.HWResolution[1] * options->header.cupsPageSize[1] / POINTS_PER_INCH));

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

  // Send feed adjustment command - only send when necessary (when any value != 0)
  // Get feed adjustment values from printer settings using helper function
  int feed_adjustment, cut_position_adjustment, backfeed_adjustment;
  bool has_adjustments = tpcl_get_feed_adjustments(printer_attrs, &feed_adjustment, &cut_position_adjustment, &backfeed_adjustment, job, NULL);

  // Only send AX command if at least one adjustment value is non-zero
  if (has_adjustments)
  {
    // Format: {AX;+/-bbb,+/-ddd,+/-ff|}
    // Feed: negative = forward (-), positive = backward (+)
    // Cut position: negative = forward (-), positive = backward (+)
    // Backfeed: negative = decrease (-), positive = increase (+)
    snprintf(
      command,
      sizeof(command),
      "{AX;%c%03d,%c%03d,%c%02d|}\n",
      (feed_adjustment >= 0) ? '+' : '-', abs(feed_adjustment),
      (cut_position_adjustment >= 0) ? '+' : '-', abs(cut_position_adjustment),
      (backfeed_adjustment >= 0) ? '+' : '-', abs(backfeed_adjustment)
    );
    papplDevicePuts(device, command);
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending feed adjustment command: %s", command);
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

      snprintf(
        command,
        sizeof(command),
        "{AY;%c%02d,%c|}\n",
        (print_darkness >= 0) ? '+' : '-',
        abs(print_darkness),
        mode_char
      );
      papplDevicePuts(device, command);
      papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending print density adjustment command: %s", command);
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

      // d: Feed speed (use default speed from driver data as hex char)
      char speed_char = '0' + driver_data.speed_default;
      if (driver_data.speed_default > 9)
        speed_char = 'A' + (driver_data.speed_default - 10);

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

      snprintf(
        command,
        sizeof(command),
        "{T%c%c%c%c%c|}\n",
        sensor_char,
        cut_char,
        feed_mode_char,
        speed_char,
        ribbon_char
      );
      papplDevicePuts(device, command);
      papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending feed command: %s", command);
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
  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending clear image buffer command: {C|}");

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
    snprintf(
      command,
      sizeof(command),
      "{SG;0000,00000,%04u,%05u,%d,",
                                                         // x_origin
                                                         // y_origin
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
    tpcl_topix_compress_line(tpcl_job->compbuf, tpcl_job->buffer);

    // Check if compression buffer is getting full, flush if needed. Also flush if this is the last line
    size_t buffer_used      = tpcl_topix_get_buffer_used(tpcl_job->compbuf);
    size_t buffer_threshold = 0xFFFF - (tpcl_job->buffer_len + ((tpcl_job->buffer_len / 8) * 3));

    if ((buffer_used > buffer_threshold) | (y == (options->header.cupsHeight - 1)))
    {
      papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Line %u: TOPIX buffer full (%lu/65535 bytes) or last line, flushing. Y offset for this image: %d (0.1mm)", y, buffer_used, tpcl_job->y_offset);

      ssize_t bytes_written = tpcl_topix_flush(tpcl_job->compbuf, device, tpcl_job->y_offset, options->header.cupsWidth, options->header.HWResolution[0], tpcl_job->gmode);

      // Y offset for next image in 0.1mm (for TOPIX)
      tpcl_job->y_offset = (int) (y + 1) * MM_PER_INCH * 10.0 / options->header.HWResolution[0];

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

  // e: Issue speed (use default speed from driver data as hex char)
  char speed_char = '0' + driver_data.speed_default;
  if (driver_data.speed_default > 9)
    speed_char = 'A' + (driver_data.speed_default - 10);

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

  // Build the XS command
  snprintf(
    command,
    sizeof(command),
    "{XS;I,%04d,%03d%c%c%c%c%c%c|}\n",
    num_copies,
    cut_interval,
    sensor_char,
    feed_mode_char,
    speed_char,
    ribbon_char,
    rotation_char,
    status_response_char
  );
  papplDevicePuts(device, command);
  papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Sending issue label command: %s", command);

  ippDelete(printer_attrs);
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
  char                     command[256];                 // Command buffer
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

  // 2. AX command - Feed adjustment (only if values are non-zero)
  int feed_adjustment, cut_position_adjustment, backfeed_adjustment;
  bool has_adjustments = tpcl_get_feed_adjustments(printer_attrs, &feed_adjustment, &cut_position_adjustment, &backfeed_adjustment, NULL, printer);

  // Only send AX command if at least one adjustment value is non-zero
  if (has_adjustments)
  {
    snprintf(
      command,
      sizeof(command),
      "{AX;%c%03d,%c%03d,%c%02d|}\n",
      (feed_adjustment >= 0) ? '+' : '-', abs(feed_adjustment),
      (cut_position_adjustment >= 0) ? '+' : '-', abs(cut_position_adjustment),
      (backfeed_adjustment >= 0) ? '+' : '-', abs(backfeed_adjustment)
    );
    papplDevicePuts(device, command);
    papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Sending feed adjustment command: %s", command);
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

    snprintf(
      command,
      sizeof(command),
      "{AY;%c%02d,%c|}\n",
      (print_darkness >= 0) ? '+' : '-',
      abs(print_darkness),
      mode_char
    );
    papplDevicePuts(device, command);
    papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Sending print density adjustment command: %s", command);
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

    // d: Feed speed (use default speed from driver data as hex char)
    char speed_char = '0' + driver_data.speed_default;
    if (driver_data.speed_default > 9)
      speed_char = 'A' + (driver_data.speed_default - 10);

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

    snprintf(
      command,
      sizeof(command),
      "{T%c%c%c%c%c|}\n",
      sensor_char,
      cut_char,
      feed_mode_char,
      speed_char,
      ribbon_char
    );
    papplDevicePuts(device, command);
    papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Sending feed command: %s", command);
  }

  // Send clear image buffer command
  snprintf(command, sizeof(command), "{C|}\n");
  papplDevicePuts(device, command);
  papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Sending clear image buffer command: %s", command);

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
    // {LC;aaaa,bbbb,cccc,dddd,e,f|}
    // e=1 for rectangle, f=line width in dots
    snprintf(
      command,
      sizeof(command),
      "{LC;%04d,%04d,%04d,%04d,1,%d|}\n",
      x1,
      y1,
      x2,
      y2,
      line_width_dots
    );
    papplDevicePuts(device, command);
    papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Sending LC command for box %d: %s", box_num, command);
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

  // e: Issue speed (use default speed from driver data as hex char)
  char speed_char = '0' + driver_data.speed_default;
  if (driver_data.speed_default > 9)
    speed_char = 'A' + (driver_data.speed_default - 10);

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

  snprintf(
    command,
    sizeof(command),
    "{XS;I,%04d,%03d%c%c%c%c%c%c|}\n",
    num_copies,
    cut_interval,
    sensor_char,
    feed_mode_char,
    speed_char,
    ribbon_char,
    rotation_char,
    status_response_char
  );
  papplDevicePuts(device, command);
  papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Sending issue label command: %s", command);

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


/*
 * 'tpcl_get_int_option()' - Get integer option from IPP attributes
 *
 * Retrieves an integer option value from printer IPP attributes.
 * Logs the retrieved value using appropriate logger (job or printer).
 * Returns the default value if attribute is not found.
 */

static int
tpcl_get_int_option(
  ipp_t                    *attrs,
  const char               *name,
  int                      default_val,
  pappl_job_t              *job,
  pappl_printer_t          *printer
)
{
  char attr_name[256];
  int value = default_val;

  snprintf(attr_name, sizeof(attr_name), "%s-default", name);
  ipp_attribute_t *attr = ippFindAttribute(attrs, attr_name, IPP_TAG_INTEGER);

  if (attr)
  {
    value = ippGetInteger(attr, 0);
    if (job)
      papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Retrieved %s from printer settings: %d", name, value);
    else if (printer)
      papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Retrieved %s from printer settings: %d", name, value);
  }
  else
  {
    if (job)
      papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Using default %s: %d", name, default_val);
    else if (printer)
      papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Using default %s: %d", name, default_val);
  }

  return value;
}


/*
 * 'tpcl_get_str_option()' - Get string option from IPP attributes
 *
 * Retrieves a string option value from printer IPP attributes.
 * Logs the retrieved value using appropriate logger (job or printer).
 * Returns the default value if attribute is not found.
 */

static const char*
tpcl_get_str_option(
  ipp_t                    *attrs,
  const char               *name,
  const char               *default_val,
  pappl_job_t              *job,
  pappl_printer_t          *printer
)
{
  char attr_name[256];
  const char *value = default_val;

  snprintf(attr_name, sizeof(attr_name), "%s-default", name);
  ipp_attribute_t *attr = ippFindAttribute(attrs, attr_name, IPP_TAG_KEYWORD);

  if (attr)
  {
    value = ippGetString(attr, 0, NULL);
    if (job)
      papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Retrieved %s from printer settings: %s", name, value);
    else if (printer)
      papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Retrieved %s from printer settings: %s", name, value);
  }
  else
  {
    if (job)
      papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Using default %s: %s", name, default_val);
    else if (printer)
      papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Using default %s: %s", name, default_val);
  }

  return value;
}


/*
 * 'tpcl_add_vendor_int_option()' - Add integer vendor option
 *
 * Registers an integer vendor option with IPP attributes for range and default value.
 */

static void
tpcl_add_vendor_int_option(
  pappl_pr_driver_data_t   *driver_data,
  ipp_t                    **driver_attrs,
  const char               *name,
  int                      min,
  int                      max,
  int                      default_val
)
{
  char supported_name[256];
  char default_name[256];

  driver_data->vendor[driver_data->num_vendor] = name;

  snprintf(supported_name, sizeof(supported_name), "%s-supported", name);
  snprintf(default_name, sizeof(default_name), "%s-default", name);

  ippAddRange(*driver_attrs, IPP_TAG_PRINTER, supported_name, min, max);
  ippAddInteger(*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, default_name, default_val);

  driver_data->num_vendor++;
}


/*
 * 'tpcl_add_vendor_str_option()' - Add string vendor option
 *
 * Registers a string vendor option with IPP attributes for supported values and default.
 */

static void
tpcl_add_vendor_str_option(
  pappl_pr_driver_data_t   *driver_data,
  ipp_t                    **driver_attrs,
  const char               *name,
  int                      num_values,
  const char               **values,
  const char               *default_val
)
{
  char supported_name[256];
  char default_name[256];

  driver_data->vendor[driver_data->num_vendor] = name;

  snprintf(supported_name, sizeof(supported_name), "%s-supported", name);
  snprintf(default_name, sizeof(default_name), "%s-default", name);

  ippAddStrings(*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, supported_name, num_values, NULL, values);
  ippAddString(*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, default_name, NULL, default_val);

  driver_data->num_vendor++;
}


/*
 * 'tpcl_get_label_dimensions()' - Calculate label dimensions from printer settings
 *
 * Retrieves label gap and roll margin from printer settings and calculates
 * label pitch and roll width based on the print dimensions.
 * Returns true on success, false on failure.
 */

static bool
tpcl_get_label_dimensions(
  ipp_t                    *printer_attrs,
  int                      print_width,
  int                      print_height,
  int                      *label_pitch,
  int                      *roll_width,
  pappl_job_t              *job,
  pappl_printer_t          *printer
)
{
  int label_gap = 50;  // Default: 5mm
  int roll_margin = 10;  // Default: 1mm

  // Get label gap from printer settings (0.1mm)
  label_gap = tpcl_get_int_option(printer_attrs, "label-gap", 50, job, printer);

  // Get roll margin from printer settings (0.1mm)
  roll_margin = tpcl_get_int_option(printer_attrs, "roll-margin", 10, job, printer);

  // Calculate label pitch and roll width from retrieved values
  *label_pitch = print_height + label_gap;
  *roll_width = print_width + roll_margin;

  if (job)
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Calculated label dimensions from page size: width=%d (0.1mm), height=%d (0.1mm), pitch=%d (0.1mm), roll=%d (0.1mm)",
                print_width, print_height, *label_pitch, *roll_width);
  else if (printer)
    papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Calculated label dimensions: width=%d (0.1mm), height=%d (0.1mm), pitch=%d (0.1mm), roll=%d (0.1mm)",
                    print_width, print_height, *label_pitch, *roll_width);

  return true;
}


/*
 * 'tpcl_get_feed_adjustments()' - Get feed adjustment values from printer settings
 *
 * Retrieves feed adjustment, cut position adjustment, and backfeed adjustment values
 * from printer settings. Returns true if at least one adjustment is non-zero, false otherwise.
 */

static bool
tpcl_get_feed_adjustments(
  ipp_t                    *printer_attrs,
  int                      *feed_adjustment,
  int                      *cut_position_adjustment,
  int                      *backfeed_adjustment,
  pappl_job_t              *job,
  pappl_printer_t          *printer
)
{
  // Get feed adjustment from printer settings (0.1mm)
  *feed_adjustment = tpcl_get_int_option(printer_attrs, "feed-adjustment", 0, job, printer);

  // Get cut position adjustment from printer settings (0.1mm)
  *cut_position_adjustment = tpcl_get_int_option(printer_attrs, "cut-position-adjustment", 0, job, printer);

  // Get backfeed adjustment from printer settings (0.1mm)
  *backfeed_adjustment = tpcl_get_int_option(printer_attrs, "backfeed-adjustment", 0, job, printer);

  // Return true if at least one adjustment is non-zero
  return (*feed_adjustment != 0 || *cut_position_adjustment != 0 || *backfeed_adjustment != 0);
}


/*
 * 'tpcl_map_sensor_type()' - Map sensor type string to TPCL character
 */

static char
tpcl_map_sensor_type(const char *sensor_type)
{
  if (strcmp(sensor_type, "none") == 0)
    return '0';
  else if (strcmp(sensor_type, "reflective") == 0)
    return '1';
  else if (strcmp(sensor_type, "transmissive-pre-print") == 0)
    return '3';
  else if (strcmp(sensor_type, "reflective-pre-print") == 0)
    return '4';
  else
    return '2';  // transmissive (default)
}


/*
 * 'tpcl_map_cut_type()' - Map cut type string to TPCL character
 */

static char
tpcl_map_cut_type(const char *cut_type)
{
  if (strcmp(cut_type, "cut") == 0)
    return '1';
  else
    return '0';  // non-cut (default)
}


/*
 * 'tpcl_map_feed_mode()' - Map feed mode string to TPCL character
 */

static char
tpcl_map_feed_mode(const char *feed_mode)
{
  if (strcmp(feed_mode, "strip-backfeed-sensor") == 0)
    return 'D';
  else if (strcmp(feed_mode, "strip-backfeed-no-sensor") == 0)
    return 'E';
  else if (strcmp(feed_mode, "partial-cut") == 0)
    return 'F';
  else
    return 'C';  // batch (default)
}
