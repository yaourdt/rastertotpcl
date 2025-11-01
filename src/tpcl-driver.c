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
// TODO: Implement testpage callback
// TODO set IPP attributes
// TODO set label processing modes according to cutter / peeler / tear bar installed or not

#include "tpcl-driver.h"
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
 * Platform-specific state file directory
 */

#ifdef __APPLE__
  #define TPCL_STATE_DIR "/Library/Caches/tpcl-printer-app"
#else
  #define TPCL_STATE_DIR "/var/cache/tpcl-printer-app"
#endif

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
  unsigned char            *last_buffer;                 // Previous line buffer (for TOPIX)
  unsigned char            *comp_buffer;                 // Compression buffer (for TOPIX)
  unsigned char            *comp_buffer_ptr;             // Current position in comp_buffer (for TOPIX)
  int                      y_offset;                     // Y offset for next image in 0.1mm (for TOPIX)
} tpcl_job_t;


/*
 * Printer state data structure for persisting label dimensions
 */

typedef struct {
  int                      last_print_width;             // Last effective print width (0.1mm)
  int                      last_print_height;            // Last effective print height (0.1mm)
  int                      last_label_gap;               // Last label gap (0.1mm)
  int                      last_roll_margin;             // Last roll margin (0.1mm)
  bool                     initialized;                  // Whether state has been initialized
} tpcl_printer_state_t;

// Global state for tracking label dimensions between jobs
static tpcl_printer_state_t g_printer_state = {0, 0, 0, 0, false};


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

static bool tpcl_load_printer_state(
  pappl_printer_t          *printer,
  tpcl_printer_state_t     *state
);

static bool tpcl_save_printer_state(
  pappl_printer_t          *printer,
  const tpcl_printer_state_t *state
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

  // - Gap beween labels in units of 0.1mm
  driver_data->vendor[driver_data->num_vendor] = "label-gap";
  ippAddRange  (*driver_attrs, IPP_TAG_PRINTER,                  "label-gap-supported", 00, 200);
  ippAddInteger(*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "label-gap-default"  , 50     );
  driver_data->num_vendor++;

  // - Roll margin in units of 0.1mm (width difference between backing paper and label)
  driver_data->vendor[driver_data->num_vendor] = "roll-margin";
  ippAddRange  (*driver_attrs, IPP_TAG_PRINTER,                  "roll-margin-supported",  0, 300);
  ippAddInteger(*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "roll-margin-default"  , 10     );
  driver_data->num_vendor++;

  // - Sensor type for label detection
  driver_data->vendor[driver_data->num_vendor] = "sensor-type";
  ippAddStrings(*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "sensor-type-supported", 3, NULL, (const char *[]){"none", "reflective", "transmissive"});
  ippAddString (*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "sensor-type-default", NULL, "transmissive");
  driver_data->num_vendor++;

  //  - Cut/non-cut selection
  driver_data->vendor[driver_data->num_vendor] = "label-cut";
  ippAddStrings(*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "label-cut-supported", 2, NULL, (const char *[]){"non-cut", "cut"});
  ippAddString (*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "label-cut-default", NULL, "non-cut");
  driver_data->num_vendor++;

  // - Cut interval (number of labels before cutting, 0=no cut, 1-100)
  driver_data->vendor[driver_data->num_vendor] = "cut-interval";
  ippAddRange  (*driver_attrs, IPP_TAG_PRINTER,                  "cut-interval-supported", 0, 100);
  ippAddInteger(*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "cut-interval-default"  , 0);
  driver_data->num_vendor++;

  // - Feed mode selection
  driver_data->vendor[driver_data->num_vendor] = "feed-mode";
  ippAddStrings(*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "feed-mode-supported", 4, NULL, (const char *[]){"batch", "strip-backfeed-sensor", "strip-backfeed-no-sensor", "partial-cut"});
  ippAddString (*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "feed-mode-default", NULL, "batch");
  driver_data->num_vendor++;

  // - Feed on label size change?
  driver_data->vendor[driver_data->num_vendor] = "feed-on-label-size-change";
  ippAddStrings(*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "feed-on-label-size-change-supported", 2, NULL, (const char *[]){"yes", "no"});
  ippAddString (*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "feed-on-label-size-change-default", NULL, "yes");
  driver_data->num_vendor++;

  // - Graphics mode selection
  driver_data->vendor[driver_data->num_vendor] = "graphics-mode";
  ippAddStrings(*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "graphics-mode-supported", 5, NULL, (const char *[]){"nibble-and", "hex-and", "topix", "nibble-or", "hex-or"});
  ippAddString (*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "graphics-mode-default", NULL, "topix");
  driver_data->num_vendor++;

  // - Dithering algorithm selection
  driver_data->vendor[driver_data->num_vendor] = "dithering-algorithm";
  ippAddStrings(*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "dithering-algorithm-supported", 3, NULL, (const char *[]){"threshold", "bayer", "clustered"});
  ippAddString (*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD, "dithering-algorithm-default", NULL, "threshold");
  driver_data->num_vendor++;

  // - Dithering threshold level (0-255, only used with 'threshold' algorithm)
  driver_data->vendor[driver_data->num_vendor] = "dithering-threshold";
  ippAddRange  (*driver_attrs, IPP_TAG_PRINTER,                  "dithering-threshold-supported", 0, 255);
  ippAddInteger(*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "dithering-threshold-default"  , 128);
  driver_data->num_vendor++;

  // - Feed adjustment value (-500 to 500 in 0.1mm units, negative = forward, positive = backward, 0 = no adjustment)
  driver_data->vendor[driver_data->num_vendor] = "feed-adjustment";
  ippAddRange  (*driver_attrs, IPP_TAG_PRINTER,                  "feed-adjustment-supported", -500, 500);
  ippAddInteger(*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "feed-adjustment-default"  , 0);
  driver_data->num_vendor++;

  // - Cut position adjustment value (-180 to 180 in 0.1mm units, negative = forward, positive = backward, 0 = no adjustment)
  driver_data->vendor[driver_data->num_vendor] = "cut-position-adjustment";
  ippAddRange  (*driver_attrs, IPP_TAG_PRINTER,                  "cut-position-adjustment-supported", -180, 180);
  ippAddInteger(*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "cut-position-adjustment-default"  , 0);
  driver_data->num_vendor++;

  // - Backfeed adjustment value (-99 to 99 in 0.1mm units, negative = decrease, positive = increase, 0 = no adjustment)
  driver_data->vendor[driver_data->num_vendor] = "backfeed-adjustment";
  ippAddRange  (*driver_attrs, IPP_TAG_PRINTER,                  "backfeed-adjustment-supported", -99, 99);
  ippAddInteger(*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "backfeed-adjustment-default"  , 0);
  driver_data->num_vendor++;

  //driver_data->num_features;                           // Number of "ipp-features-supported" values TODO
  //driver_data->*features[PAPPL_MAX_VENDOR];            // "ipp-features-supported" values TODO

  //
  // Model-agnostic printer options
  //

  // Configure dithering based on default IPP attributes
  const char *dither_algo = "threshold";
  ipp_attribute_t *dither_algo_attr = ippFindAttribute(*driver_attrs, "dithering-algorithm-default", IPP_TAG_KEYWORD);
  if (dither_algo_attr)
  {
    dither_algo = ippGetString(dither_algo_attr, 0, NULL);
  }

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
    int dither_threshold = 128;
    ipp_attribute_t *dither_threshold_attr = ippFindAttribute(*driver_attrs, "dithering-threshold-default", IPP_TAG_INTEGER);
    if (dither_threshold_attr)
    {
      dither_threshold = ippGetInteger(dither_threshold_attr, 0);
    }
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
      driver_data->vendor[driver_data->num_vendor] = "print-speed";
      ippAddRange  (*driver_attrs, IPP_TAG_PRINTER,                  "print-speed-supported", tpcl_printer_properties[i].print_speeds[0], tpcl_printer_properties[i].print_speeds[2]);
      ippAddInteger(*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "print-speed-default", tpcl_printer_properties[i].print_speeds[1]);
      driver_data->num_vendor++;
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
      driver_data->borderless                   =  true; // Borderless margins supported?
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
      driver_data->top_offset_supported[0]      = -5000; // Minimum top printing offset supported
      driver_data->top_offset_supported[1]      =  5000; // Maximum top printing offset supported
      driver_data->media_ready[0].top_offset    =     0; // Default top offset in hundredths of millimeters

      // Printer darkness (workaround due to PAPPL web interface limitations)
      driver_data->darkness_supported           =     0; // Printer darkness
      driver_data->darkness_default             =     0; // Default darkness adjustment at printer power-on
      driver_data->darkness_configured          =     0; // Currently configured printing darkness
      driver_data->vendor[driver_data->num_vendor] = "print-darkness";
      ippAddRange  (*driver_attrs, IPP_TAG_PRINTER,                  "print-darkness-supported", -10, 10);
      ippAddInteger(*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER, "print-darkness-default",         0);
      driver_data->num_vendor++;

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

  // Get label gap from printer settings (0.1mm)
  ipp_attribute_t *gap_attr = ippFindAttribute(printer_attrs, "label-gap-default", IPP_TAG_INTEGER);
  if (gap_attr)
  {
    label_pitch = print_height + ippGetInteger(gap_attr, 0);
    papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Retrieved label gap from printer settings: %d (0.1mm)", label_pitch - print_height);
  }
  else
  {
    label_pitch = print_height + 50;
    papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Using default label gap of 5mm");
  }

  // Get roll margin from printer settings (0.1mm)
  ipp_attribute_t *margin_attr = ippFindAttribute(printer_attrs, "roll-margin-default", IPP_TAG_INTEGER);
  if (margin_attr)
  {
    roll_width = print_width + ippGetInteger(margin_attr, 0);
    papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Retrieved roll margin from printer settings: %d (0.1mm)", roll_width - print_width);
  }
  else
  {
    roll_width  = print_width + 10;
    papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Using default roll margin of 1mm");
  }

  papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Calculated label dimensions: width=%d (0.1mm), height=%d (0.1mm), pitch=%d (0.1mm), roll=%d (0.1mm)", print_width, print_height, label_pitch, roll_width);

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
  // a: Sensor type (0=none, 1=reflective, 2=transmissive)
  char sensor_char = '2';
  ipp_attribute_t *sensor_attr = ippFindAttribute(printer_attrs, "sensor-type-default", IPP_TAG_KEYWORD);
  if (sensor_attr)
  {
    const char *sensor_type = ippGetString(sensor_attr, 0, NULL);
    if (strcmp(sensor_type, "none") == 0)
      sensor_char = '0';
    else if (strcmp(sensor_type, "reflective") == 0)
      sensor_char = '1';
  }

  // b: Cut selection (0=non-cut, 1=cut)
  char cut_char = '0';
  ipp_attribute_t *cut_attr = ippFindAttribute(printer_attrs, "label-cut-default", IPP_TAG_KEYWORD);
  if (cut_attr)
  {
    const char *cut_type = ippGetString(cut_attr, 0, NULL);
    if (strcmp(cut_type, "cut") == 0)
      cut_char = '1';
  }

  // c: Feed mode (C=batch, D=strip with backfeed sensor valid, E=strip with backfeed sensor ignored, F=partial-cut)
  char feed_mode_char = 'C';
  ipp_attribute_t *feed_mode_attr = ippFindAttribute(printer_attrs, "feed-mode-default", IPP_TAG_KEYWORD);
  if (feed_mode_attr)
  {
    const char *feed_mode = ippGetString(feed_mode_attr, 0, NULL);
    if (strcmp(feed_mode, "strip-backfeed-sensor") == 0)
      feed_mode_char = 'D';
    else if (strcmp(feed_mode, "strip-backfeed-no-sensor") == 0)
      feed_mode_char = 'E';
    else if (strcmp(feed_mode, "partial-cut") == 0)
      feed_mode_char = 'F';
  }

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
  const char *graphics_mode = NULL;
  ipp_attribute_t *graphics_mode_attr = ippFindAttribute(printer_attrs, "graphics-mode-default", IPP_TAG_KEYWORD);
  if (graphics_mode_attr)
  {
    graphics_mode = ippGetString(graphics_mode_attr, 0, NULL);
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Retrieved graphics mode from printer settings: %s", graphics_mode);
  }
  if (graphics_mode)
  {
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
  }
  else
  {
    // Default to TOPIX if not specified
    tpcl_job->gmode = TEC_GMODE_TOPIX;
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Graphics mode not specified, defaulting to TOPIX (%d)", tpcl_job->gmode);
  }

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
      ippDelete(printer_attrs);
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
      ippDelete(printer_attrs);
      return false;
    }
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "HEX mode buffer allocated: line=%u bytes", options->header.cupsBytesPerLine);
  }

  // Calculate label dimensions from page size
  // cupsPageSize is in points (1/72 inch), convert to 0.1mm: points * 25.4 * 10 / 72
  tpcl_job->print_width  = (int)(options->header.cupsPageSize[0] * MM_PER_INCH * 10.0 / POINTS_PER_INCH);  // Effective print width (0.1mm)
  tpcl_job->print_height = (int)(options->header.cupsPageSize[1] * MM_PER_INCH * 10.0 / POINTS_PER_INCH);  // Effective print height (0.1mm)

  // Get label gap from printer settings (0.1mm)
  int label_gap = 50;  // Default: 5mm
  ipp_attribute_t *gap_attr = ippFindAttribute(printer_attrs, "label-gap-default", IPP_TAG_INTEGER);
  if (gap_attr)
  {
    label_gap = ippGetInteger(gap_attr, 0);
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Retrieved label gap from printer settings: %d (0.1mm)", label_gap);
  }
  else
  {
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Using default label gap of 5mm");
  }

  // Get roll margin from printer settings (0.1mm)
  int roll_margin = 10;  // Default: 1mm
  ipp_attribute_t *margin_attr = ippFindAttribute(printer_attrs, "roll-margin-default", IPP_TAG_INTEGER);
  if (margin_attr)
  {
    roll_margin = ippGetInteger(margin_attr, 0);
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Retrieved roll margin from printer settings: %d (0.1mm)", roll_margin);
  }
  else
  {
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Using default roll margin of 1mm");
  }

  // Calculate label pitch and roll width from retrieved values
  tpcl_job->label_pitch = tpcl_job->print_height + label_gap;
  tpcl_job->roll_width  = tpcl_job->print_width + roll_margin;

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
  // Get feed adjustment values from printer settings
  int feed_adjustment = 0;  // Default: no adjustment
  ipp_attribute_t *feed_adj_attr = ippFindAttribute(printer_attrs, "feed-adjustment-default", IPP_TAG_INTEGER);
  if (feed_adj_attr)
  {
    feed_adjustment = ippGetInteger(feed_adj_attr, 0);
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Retrieved feed adjustment from printer settings: %d (0.1mm)", feed_adjustment);
  }

  int cut_position_adjustment = 0;  // Default: no adjustment
  ipp_attribute_t *cut_pos_adj_attr = ippFindAttribute(printer_attrs, "cut-position-adjustment-default", IPP_TAG_INTEGER);
  if (cut_pos_adj_attr)
  {
    cut_position_adjustment = ippGetInteger(cut_pos_adj_attr, 0);
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Retrieved cut position adjustment from printer settings: %d (0.1mm)", cut_position_adjustment);
  }

  int backfeed_adjustment = 0;  // Default: no adjustment
  ipp_attribute_t *backfeed_adj_attr = ippFindAttribute(printer_attrs, "backfeed-adjustment-default", IPP_TAG_INTEGER);
  if (backfeed_adj_attr)
  {
    backfeed_adjustment = ippGetInteger(backfeed_adj_attr, 0);
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Retrieved backfeed adjustment from printer settings: %d (0.1mm)", backfeed_adjustment);
  }

  // Only send AX command if at least one adjustment value is non-zero
  if (feed_adjustment != 0 || cut_position_adjustment != 0 || backfeed_adjustment != 0)
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
  int print_darkness = 0;  // Default: no adjustment
  ipp_attribute_t *darkness_attr = ippFindAttribute(printer_attrs, "print-darkness-default", IPP_TAG_INTEGER);
  if (darkness_attr)
  {
    print_darkness = ippGetInteger(darkness_attr, 0);
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Retrieved print darkness from printer settings: %d", print_darkness);
  }

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

  // Load previous printer state from file if not already loaded
  if (!g_printer_state.initialized)
  {
    tpcl_load_printer_state(printer, &g_printer_state);
  }

  // Check if label dimensions have changed from previous job
  bool label_size_changed = false;

  if (g_printer_state.initialized)
  {
    if (g_printer_state.last_print_width != tpcl_job->print_width ||
        g_printer_state.last_print_height != tpcl_job->print_height ||
        g_printer_state.last_label_gap != label_gap ||
        g_printer_state.last_roll_margin != roll_margin)
    {
      label_size_changed = true;
      papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "Label size changed: old(%d×%d+%d/%d) → new(%d×%d+%d/%d) [width×height+gap/margin in 0.1mm]",
        g_printer_state.last_print_width, g_printer_state.last_print_height,
        g_printer_state.last_label_gap, g_printer_state.last_roll_margin,
        tpcl_job->print_width, tpcl_job->print_height, label_gap, roll_margin);
    }
  }
  else
  {
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "No previous label dimensions found, this is likely the first job");
    label_size_changed = true;  // Treat first job as changed to initialize
  }

  // Update stored label dimensions for next job
  g_printer_state.last_print_width = tpcl_job->print_width;
  g_printer_state.last_print_height = tpcl_job->print_height;
  g_printer_state.last_label_gap = label_gap;
  g_printer_state.last_roll_margin = roll_margin;
  g_printer_state.initialized = true;

  // Save state to file for persistence across restarts
  tpcl_save_printer_state(printer, &g_printer_state);

  // If label size changed and feed-on-label-size-change is enabled, send feed command
  const char *feed_on_change_str = NULL;
  ipp_attribute_t *feed_on_change_attr = ippFindAttribute(printer_attrs, "feed-on-label-size-change-default", IPP_TAG_KEYWORD);
  if (feed_on_change_attr)
  {
    feed_on_change_str = ippGetString(feed_on_change_attr, 0, NULL);
  }
  bool should_feed = label_size_changed && feed_on_change_str && (strcmp(feed_on_change_str, "yes") == 0);

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
      // a: Sensor type (0=none, 1=reflective, 2=transmissive)
      char sensor_char = '2';
      const char *sensor_type = NULL;
      ipp_attribute_t *sensor_type_attr = ippFindAttribute(printer_attrs, "sensor-type-default", IPP_TAG_KEYWORD);
      if (sensor_type_attr)
      {
        sensor_type = ippGetString(sensor_type_attr, 0, NULL);
        if (strcmp(sensor_type, "none") == 0)
          sensor_char = '0';
        else if (strcmp(sensor_type, "reflective") == 0)
          sensor_char = '1';
      }

      // b: Cut selection (0=non-cut, 1=cut)
      char cut_char = '0';
      const char *cut_type = NULL;
      ipp_attribute_t *cut_type_attr = ippFindAttribute(printer_attrs, "label-cut-default", IPP_TAG_KEYWORD);
      if (cut_type_attr)
      {
        cut_type = ippGetString(cut_type_attr, 0, NULL);
        if (strcmp(cut_type, "cut") == 0)
          cut_char = '1';
      }

      // c: Feed mode (C=batch, D=strip with backfeed sensor valid, E=strip with backfeed sensor ignored, F=partial-cut)
      char feed_mode_char = 'C';
      const char *feed_mode = NULL;
      ipp_attribute_t *feed_mode_attr = ippFindAttribute(printer_attrs, "feed-mode-default", IPP_TAG_KEYWORD);
      if (feed_mode_attr)
      {
        feed_mode = ippGetString(feed_mode_attr, 0, NULL);
        if (strcmp(feed_mode, "strip-backfeed-sensor") == 0)
          feed_mode_char = 'D';
        else if (strcmp(feed_mode, "strip-backfeed-no-sensor") == 0)
          feed_mode_char = 'E';
        else if (strcmp(feed_mode, "partial-cut") == 0)
          feed_mode_char = 'F';
      }

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
  int cut_interval = 0;
  ipp_attribute_t *cut_interval_attr = ippFindAttribute(printer_attrs, "cut-interval-default", IPP_TAG_INTEGER);
  if (cut_interval_attr)
  {
    cut_interval = ippGetInteger(cut_interval_attr, 0);
    if (cut_interval < 0 || cut_interval > 100)
    {
      papplLogJob(job, PAPPL_LOGLEVEL_ERROR, "Invalid cut interval %d, must be in range [0-100]", cut_interval);
      ippDelete(printer_attrs);
      tpcl_free_job_buffers(job, tpcl_job);
      return false;
    }
  }

  // c: Type of sensor (0=none, 1=reflective, 2=transmissive)
  char sensor_char = '2';
  const char *sensor_type = NULL;
  ipp_attribute_t *sensor_type_attr = ippFindAttribute(printer_attrs, "sensor-type-default", IPP_TAG_KEYWORD);
  if (sensor_type_attr)
  {
    sensor_type = ippGetString(sensor_type_attr, 0, NULL);
    if (strcmp(sensor_type, "none") == 0)
      sensor_char = '0';
    else if (strcmp(sensor_type, "reflective") == 0)
      sensor_char = '1';
  }

  // d: Issue mode (C=batch, D=strip with backfeed sensor valid, E=strip with backfeed sensor ignored, F=partial-cut)
  char feed_mode_char = 'C';
  const char *feed_mode = NULL;
  ipp_attribute_t *feed_mode_attr = ippFindAttribute(printer_attrs, "feed-mode-default", IPP_TAG_KEYWORD);
  if (feed_mode_attr)
  {
    feed_mode = ippGetString(feed_mode_attr, 0, NULL);
    if (strcmp(feed_mode, "strip-backfeed-sensor") == 0)
      feed_mode_char = 'D';
    else if (strcmp(feed_mode, "strip-backfeed-no-sensor") == 0)
      feed_mode_char = 'E';
    else if (strcmp(feed_mode, "partial-cut") == 0)
      feed_mode_char = 'F';
  }

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
 * Cleans up printer resources including the persistent state file.
 */

void tpcl_delete_cb(
  pappl_printer_t          *printer,
  pappl_pr_driver_data_t   *data
)
{
  char filepath[512];
  const char *printer_name = papplPrinterGetName(printer);

  // Construct state file path
  snprintf(filepath, sizeof(filepath), "%s/%s.state", TPCL_STATE_DIR, printer_name);

  // Delete state file if it exists
  if (unlink(filepath) == 0)
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_INFO, "Deleted state file: %s", filepath);
  }
  else if (errno == ENOENT)
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "No state file to delete at %s", filepath);
  }
  else
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_WARN, "Failed to delete state file %s: %s", filepath, strerror(errno));
  }

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


/*
 * 'tpcl_load_printer_state()' - Load printer state from file
 *
 * Loads the last used label dimensions from /var/cache/tpcl-printer-app/<printer-name>.state
 * Returns true if state was loaded successfully, false otherwise.
 */

static bool
tpcl_load_printer_state(
  pappl_printer_t          *printer,
  tpcl_printer_state_t     *state
)
{
  char filepath[512];
  const char *printer_name = papplPrinterGetName(printer);
  FILE *fp;
  char line[512];
  int loaded_width = -1, loaded_height = -1, loaded_gap = -1, loaded_margin = -1;

  // Construct file path
  snprintf(filepath, sizeof(filepath), "%s/%s.state", TPCL_STATE_DIR, printer_name);

  // Try to open the state file
  fp = fopen(filepath, "r");
  if (!fp)
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "No previous state file found at %s", filepath);
    return false;
  }

  // Read state from file
  while (fgets(line, sizeof(line), fp))
  {
    // Remove trailing newline
    line[strcspn(line, "\n")] = '\0';

    if (sscanf(line, "last_print_width=%d", &loaded_width) == 1)
      continue;
    if (sscanf(line, "last_print_height=%d", &loaded_height) == 1)
      continue;
    if (sscanf(line, "last_label_gap=%d", &loaded_gap) == 1)
      continue;
    if (sscanf(line, "last_roll_margin=%d", &loaded_margin) == 1)
      continue;
  }

  fclose(fp);

  // Validate that we loaded all required fields
  if (loaded_width < 0 || loaded_height < 0 || loaded_gap < 0 || loaded_margin < 0)
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_WARN, "Incomplete state file at %s, ignoring", filepath);
    return false;
  }

  // Copy loaded state
  state->last_print_width = loaded_width;
  state->last_print_height = loaded_height;
  state->last_label_gap = loaded_gap;
  state->last_roll_margin = loaded_margin;
  state->initialized = true;

  papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Loaded state from %s: width=%d, height=%d, gap=%d, margin=%d",
    filepath, loaded_width, loaded_height, loaded_gap, loaded_margin);

  return true;
}


/*
 * 'tpcl_save_printer_state()' - Save printer state to file
 *
 * Saves the current label dimensions to /var/cache/tpcl-printer-app/<printer-name>.state
 * Returns true if state was saved successfully, false otherwise.
 */

static bool
tpcl_save_printer_state(
  pappl_printer_t          *printer,
  const tpcl_printer_state_t *state
)
{
  char filepath[512];
  const char *printer_name = papplPrinterGetName(printer);
  FILE *fp;

  // Create directory if it doesn't exist
  if (mkdir(TPCL_STATE_DIR, 0755) != 0 && errno != EEXIST)
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Failed to create directory %s: %s", TPCL_STATE_DIR, strerror(errno));
    return false;
  }

  // Construct file path
  snprintf(filepath, sizeof(filepath), "%s/%s.state", TPCL_STATE_DIR, printer_name);

  // Open file for writing
  fp = fopen(filepath, "w");
  if (!fp)
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Failed to open state file %s for writing: %s", filepath, strerror(errno));
    return false;
  }

  // Write state to file
  fprintf(fp, "# TPCL Printer State File\n");
  fprintf(fp, "# Auto-generated - do not edit manually\n");
  fprintf(fp, "last_print_width=%d\n", state->last_print_width);
  fprintf(fp, "last_print_height=%d\n", state->last_print_height);
  fprintf(fp, "last_label_gap=%d\n", state->last_label_gap);
  fprintf(fp, "last_roll_margin=%d\n", state->last_roll_margin);

  fclose(fp);

  papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Saved state to %s: width=%d, height=%d, gap=%d, margin=%d",
    filepath, state->last_print_width, state->last_print_height, state->last_label_gap, state->last_roll_margin);

  return true;
}
