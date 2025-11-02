/*
 * TPCL Configuration Helpers Implementation
 *
 * Configuration mapping, vendor option setup, and conversion helpers
 * for Toshiba TEC label printers.
 *
 * Copyright © 2025 by Mark Dornbach
 *
 * Licensed under GNU GPL v3.
 */

#include "tpcl-config.h"
#include "tpcl-ipp-utils.h"
#include "tpcl-driver.h"
#include "dithering.h"
#include "icon-48.h"
#include "icon-128.h"
#include "icon-512.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

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
 * 'tpcl_setup_vendor_options()' - Setup vendor-specific configuration options
 */

bool
tpcl_setup_vendor_options(
  pappl_pr_driver_data_t   *driver_data,
  ipp_t                    **driver_attrs
)
{
  // Initialize vendor option count
  driver_data->num_vendor = 0;

  // Create IPP attributes if needed
  if (!*driver_attrs)
    *driver_attrs = ippNew();

  if (!*driver_attrs)
    return false;

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

  // Dithering algorithm selection for photo content
  tpcl_add_vendor_str_option(driver_data, driver_attrs, "dithering-algorithm-photo", 3, (const char *[]){"threshold", "bayer", "clustered"}, "threshold");

  // Dithering threshold level (0-255, only used with 'threshold' algorithm)
  tpcl_add_vendor_int_option(driver_data, driver_attrs, "dithering-threshold", 0, 255, 128);

  // Feed adjustment value (-500 to 500 in 0.1mm units, negative = forward, positive = backward, 0 = no adjustment)
  tpcl_add_vendor_int_option(driver_data, driver_attrs, "feed-adjustment", -500, 500, 0);

  // Cut position adjustment value (-180 to 180 in 0.1mm units, negative = forward, positive = backward, 0 = no adjustment)
  tpcl_add_vendor_int_option(driver_data, driver_attrs, "cut-position-adjustment", -180, 180, 0);

  // Backfeed adjustment value (-99 to 99 in 0.1mm units, negative = decrease, positive = increase, 0 = no adjustment)
  tpcl_add_vendor_int_option(driver_data, driver_attrs, "backfeed-adjustment", -99, 99, 0);

  return true;
}


/*
 * 'tpcl_setup_driver_common()' - Setup common model-agnostic driver options
 */

bool
tpcl_setup_driver_common(
  pappl_pr_driver_data_t   *driver_data,
  ipp_t                    **driver_attrs
)
{
  // Configure dithering for general content based on default IPP attributes
  const char *dither_algo = tpcl_get_str_option(*driver_attrs, "dithering-algorithm", "threshold", NULL, NULL);

  if (strcmp(dither_algo, "bayer") == 0)
  {
    dither_bayer16(driver_data->gdither);                // Dithering for 'auto', 'text', and 'graphic'
  }
  else if (strcmp(dither_algo, "clustered") == 0)
  {
    dither_clustered16(driver_data->gdither);
  }
  else
  {
    int dither_threshold = tpcl_get_int_option(*driver_attrs, "dithering-threshold", 128, NULL, NULL);
    dither_threshold16(driver_data->gdither, (unsigned char)dither_threshold);
  }

  // Configure dithering for photo content based on separate photo algorithm option
  const char *dither_algo_photo = tpcl_get_str_option(*driver_attrs, "dithering-algorithm-photo", "threshold", NULL, NULL);

  if (strcmp(dither_algo_photo, "bayer") == 0)
  {
    dither_bayer16(driver_data->pdither);                // Dithering for 'photo'
  }
  else if (strcmp(dither_algo_photo, "clustered") == 0)
  {
    dither_clustered16(driver_data->pdither);
  }
  else
  {
    int dither_threshold = tpcl_get_int_option(*driver_attrs, "dithering-threshold", 128, NULL, NULL);
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
  driver_data->tear_offset_configured   =   0;           // Default offset when in mode for tearing labels

  return true;
}


/*
 * 'tpcl_setup_driver_model()' - Setup model-specific driver options
 */

bool
tpcl_setup_driver_model(
  pappl_system_t           *system,
  const char               *driver_name,
  pappl_pr_driver_data_t   *driver_data,
  ipp_t                    **driver_attrs
)
{
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
      return true;
    }
  }

  // Driver not found in table
  papplLog(system, PAPPL_LOGLEVEL_ERROR, "Driver '%s' not found in driver table", driver_name);
  return false;
}


/*
 * 'tpcl_map_sensor_type()' - Map sensor type string to TPCL character
 */

char
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

char
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

char
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
