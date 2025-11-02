/*
 * TPCL Configuration Helpers Header
 *
 * Configuration mapping, vendor option setup, and conversion helpers
 * for Toshiba TEC label printers.
 *
 * Copyright © 2025 by Mark Dornbach
 *
 * Licensed under GNU GPL v3.
 */

#ifndef TPCL_CONFIG_H
#define TPCL_CONFIG_H

#include <pappl/pappl.h>


/*
 * Constants for conversion
 */

#define POINTS_PER_INCH 72.0
#define MM_PER_INCH     25.4


/*
 * 'tpcl_setup_vendor_options()' - Setup vendor-specific configuration options
 *
 * Registers all Toshiba TEC specific vendor options with the driver.
 * This includes label dimensions, sensor types, feed modes, graphics modes,
 * dithering options, and various adjustment parameters. 
 * PAPPL allows max. 32 vendor attributes.
 *
 * Parameters:
 *   driver_data  - Driver data structure to register vendor options
 *   driver_attrs - IPP attributes to add options to
 *
 * Returns:
 *   true on success, false on failure
 */

bool tpcl_setup_vendor_options(
  pappl_pr_driver_data_t   *driver_data,
  ipp_t                    **driver_attrs
);


/*
 * 'tpcl_setup_driver_common()' - Setup common model-agnostic driver options
 *
 * Configures common driver capabilities that apply to all TPCL printers,
 * including dithering setup, printer icons, color modes, raster types,
 * label modes, and other model-independent settings.
 *
 * Parameters:
 *   driver_data  - Driver data structure to configure
 *   driver_attrs - IPP attributes (for retrieving dithering settings)
 *
 * Returns:
 *   true on success, false on failure
 */

bool tpcl_setup_driver_common(
  pappl_pr_driver_data_t   *driver_data,
  ipp_t                    **driver_attrs
);


/*
 * 'tpcl_setup_driver_model()' - Setup model-specific driver options
 *
 * Configures model-specific driver settings based on the driver name,
 * including resolutions, print speeds, media sizes, media types, and
 * other printer-specific capabilities.
 *
 * Parameters:
 *   system       - PAPPL system for logging
 *   driver_name  - Name of the driver to configure
 *   driver_data  - Driver data structure to configure
 *   driver_attrs - IPP attributes for vendor options
 *
 * Returns:
 *   true on success, false if driver not found or configuration failed
 */

bool tpcl_setup_driver_model(
  pappl_system_t           *system,
  const char               *driver_name,
  pappl_pr_driver_data_t   *driver_data,
  ipp_t                    **driver_attrs
);


/*
 * 'tpcl_map_sensor_type()' - Map sensor type string to TPCL character
 *
 * Converts a sensor type string to the corresponding TPCL command character.
 *
 * Parameters:
 *   sensor_type - Sensor type string:
 *                 "none" -> '0'
 *                 "reflective" -> '1'
 *                 "transmissive" -> '2' (default)
 *                 "transmissive-pre-print" -> '3'
 *                 "reflective-pre-print" -> '4'
 *
 * Returns:
 *   TPCL character code for the sensor type
 */

char tpcl_map_sensor_type(
  const char               *sensor_type
);


/*
 * 'tpcl_map_cut_type()' - Map cut type string to TPCL character
 *
 * Converts a cut type string to the corresponding TPCL command character.
 *
 * Parameters:
 *   cut_type - Cut type string:
 *              "cut" -> '1'
 *              "non-cut" -> '0' (default)
 *
 * Returns:
 *   TPCL character code for the cut type
 */

char tpcl_map_cut_type(
  const char               *cut_type
);


/*
 * 'tpcl_map_feed_mode()' - Map feed mode string to TPCL character
 *
 * Converts a feed mode string to the corresponding TPCL command character.
 *
 * Parameters:
 *   feed_mode - Feed mode string:
 *               "batch" -> 'C' (default)
 *               "strip-backfeed-sensor" -> 'D'
 *               "strip-backfeed-no-sensor" -> 'E'
 *               "partial-cut" -> 'F'
 *
 * Returns:
 *   TPCL character code for the feed mode
 */

char tpcl_map_feed_mode(
  const char               *feed_mode
);


#endif // TPCL_CONFIG_H
