/*
 * TPCL IPP Helpers Header
 *
 * IPP attribute handling helpers for Toshiba TEC label printers.
 *
 * Copyright © 2025 by Mark Dornbach
 *
 * Licensed under GNU GPL v3.
 */

#ifndef TPCL_IPP_HELPERS_H
#define TPCL_IPP_HELPERS_H

#include <pappl/pappl.h>

/*
 * 'tpcl_get_int_option()' - Get integer option from IPP attributes
 *
 * Retrieves an integer option value from IPP attributes by looking for
 * an attribute named "<name>-default".
 *
 * Parameters:
 *   attrs        - IPP attributes (usually printer driver attributes)
 *   name         - Base name of the option (without "-default" suffix)
 *   default_val  - Default value to return if attribute not found
 *   job          - Job for logging (can be NULL)
 *   printer      - Printer for logging (can be NULL)
 *
 * Returns:
 *   Integer value from attribute or default_val if not found
 */

int tpcl_get_int_option(ipp_t *attrs, const char *name, int default_val,
			pappl_job_t *job, pappl_printer_t *printer);

/*
 * 'tpcl_get_str_option()' - Get string option from IPP attributes
 *
 * Retrieves a string/keyword option value from IPP attributes by looking
 * for an attribute named "<name>-default".
 *
 * Parameters:
 *   attrs        - IPP attributes (usually printer driver attributes)
 *   name         - Base name of the option (without "-default" suffix)
 *   default_val  - Default string to return if attribute not found
 *   job          - Job for logging (can be NULL)
 *   printer      - Printer for logging (can be NULL)
 *
 * Returns:
 *   String value from attribute or default_val if not found
 */

const char *tpcl_get_str_option(ipp_t *attrs, const char *name,
				const char *default_val, pappl_job_t *job,
				pappl_printer_t *printer);

/*
 * 'tpcl_add_vendor_int_option()' - Add integer vendor option
 *
 * Registers an integer vendor option with IPP attributes, adding both
 * the supported range and default value.
 *
 * Parameters:
 *   driver_data  - Driver data structure to register vendor option
 *   driver_attrs - IPP attributes to add option to
 *   name         - Base name of the option
 *   min          - Minimum value for the range
 *   max          - Maximum value for the range
 *   default_val  - Default value
 */

void tpcl_add_vendor_int_option(pappl_pr_driver_data_t *driver_data,
				ipp_t **driver_attrs, const char *name, int min,
				int max, int default_val);

/*
 * 'tpcl_add_vendor_str_option()' - Add string vendor option
 *
 * Registers a string/keyword vendor option with IPP attributes, adding
 * both the supported values and default value.
 *
 * Parameters:
 *   driver_data  - Driver data structure to register vendor option
 *   driver_attrs - IPP attributes to add option to
 *   name         - Base name of the option
 *   num_values   - Number of supported values
 *   values       - Array of supported value strings
 *   default_val  - Default value string
 */

void tpcl_add_vendor_str_option(pappl_pr_driver_data_t *driver_data,
				ipp_t **driver_attrs, const char *name,
				int num_values, const char **values,
				const char *default_val);

/*
 * 'tpcl_get_label_dimensions()' - Calculate label dimensions from printer settings
 *
 * Retrieves label gap and roll margin from printer IPP attributes and
 * calculates the label pitch and roll width based on print dimensions.
 *
 * Parameters:
 *   printer_attrs - Printer IPP attributes
 *   print_width   - Print width in 0.1mm
 *   print_height  - Print height in 0.1mm
 *   label_pitch   - Output: label pitch (height + gap) in 0.1mm
 *   roll_width    - Output: roll width (width + margin) in 0.1mm
 *   job           - Job for logging (can be NULL)
 *   printer       - Printer for logging (can be NULL)
 *
 * Returns:
 *   true on success, false on failure
 */

bool tpcl_get_label_dimensions(ipp_t *printer_attrs, int print_width,
			       int print_height, int *label_pitch,
			       int *roll_width, pappl_job_t *job,
			       pappl_printer_t *printer);

/*
 * 'tpcl_get_feed_adjustments()' - Get feed adjustment values from printer settings
 *
 * Retrieves feed adjustment, cut position adjustment, and backfeed adjustment
 * values from printer IPP attributes.
 *
 * Parameters:
 *   printer_attrs            - Printer IPP attributes
 *   feed_adjustment          - Output: feed adjustment in 0.1mm
 *   cut_position_adjustment  - Output: cut position adjustment in 0.1mm
 *   backfeed_adjustment      - Output: backfeed adjustment in 0.1mm
 *   job                      - Job for logging (can be NULL)
 *   printer                  - Printer for logging (can be NULL)
 *
 * Returns:
 *   true if at least one adjustment is non-zero, false otherwise
 */

bool tpcl_get_feed_adjustments(ipp_t *printer_attrs, int *feed_adjustment,
			       int *cut_position_adjustment,
			       int *backfeed_adjustment, pappl_job_t *job,
			       pappl_printer_t *printer);

#endif // TPCL_IPP_HELPERS_H
