/*
 * TPCL IPP Helpers Implementation
 *
 * IPP attribute handling helpers for Toshiba TEC label printers.
 *
 * Copyright © 2025 by Mark Dornbach
 *
 * Licensed under GNU GPL v3.
 */

#include "tpcl-ipp-utils.h"
#include <stdio.h>

/*
 * 'tpcl_get_int_option()' - Get integer option from IPP attributes
 */

int tpcl_get_int_option(ipp_t *attrs, const char *name, int default_val,
			pappl_job_t *job, pappl_printer_t *printer)
{
	char attr_name[256];
	int value = default_val;

	snprintf(attr_name, sizeof(attr_name), "%s-default", name);
	ipp_attribute_t *attr =
		ippFindAttribute(attrs, attr_name, IPP_TAG_INTEGER);

	if (attr) {
		value = ippGetInteger(attr, 0);
		if (job)
			papplLogJob(job, PAPPL_LOGLEVEL_DEBUG,
				    "Retrieved %s from printer settings: %d",
				    name, value);
		else if (printer)
			papplLogPrinter(
				printer, PAPPL_LOGLEVEL_DEBUG,
				"Retrieved %s from printer settings: %d", name,
				value);
	} else {
		if (job)
			papplLogJob(job, PAPPL_LOGLEVEL_DEBUG,
				    "Using default %s: %d", name, default_val);
		else if (printer)
			papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG,
					"Using default %s: %d", name,
					default_val);
	}

	return value;
}

/*
 * 'tpcl_get_str_option()' - Get string option from IPP attributes
 */

const char *tpcl_get_str_option(ipp_t *attrs, const char *name,
				const char *default_val, pappl_job_t *job,
				pappl_printer_t *printer)
{
	char attr_name[256];
	const char *value = default_val;

	snprintf(attr_name, sizeof(attr_name), "%s-default", name);
	ipp_attribute_t *attr =
		ippFindAttribute(attrs, attr_name, IPP_TAG_KEYWORD);

	if (attr) {
		value = ippGetString(attr, 0, NULL);
		if (job)
			papplLogJob(job, PAPPL_LOGLEVEL_DEBUG,
				    "Retrieved %s from printer settings: %s",
				    name, value);
		else if (printer)
			papplLogPrinter(
				printer, PAPPL_LOGLEVEL_DEBUG,
				"Retrieved %s from printer settings: %s", name,
				value);
	} else {
		if (job)
			papplLogJob(job, PAPPL_LOGLEVEL_DEBUG,
				    "Using default %s: %s", name, default_val);
		else if (printer)
			papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG,
					"Using default %s: %s", name,
					default_val);
	}

	return value;
}

/*
 * 'tpcl_add_vendor_int_option()' - Add integer vendor option
 */

void tpcl_add_vendor_int_option(pappl_pr_driver_data_t *driver_data,
				ipp_t **driver_attrs, const char *name, int min,
				int max, int default_val)
{
	char supported_name[256];
	char default_name[256];

	driver_data->vendor[driver_data->num_vendor] = name;

	snprintf(supported_name, sizeof(supported_name), "%s-supported", name);
	snprintf(default_name, sizeof(default_name), "%s-default", name);

	ippAddRange(*driver_attrs, IPP_TAG_PRINTER, supported_name, min, max);
	ippAddInteger(*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_INTEGER,
		      default_name, default_val);

	driver_data->num_vendor++;
}

/*
 * 'tpcl_add_vendor_str_option()' - Add string vendor option
 */

void tpcl_add_vendor_str_option(pappl_pr_driver_data_t *driver_data,
				ipp_t **driver_attrs, const char *name,
				int num_values, const char **values,
				const char *default_val)
{
	char supported_name[256];
	char default_name[256];

	driver_data->vendor[driver_data->num_vendor] = name;

	snprintf(supported_name, sizeof(supported_name), "%s-supported", name);
	snprintf(default_name, sizeof(default_name), "%s-default", name);

	ippAddStrings(*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
		      supported_name, num_values, NULL, values);
	ippAddString(*driver_attrs, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
		     default_name, NULL, default_val);

	driver_data->num_vendor++;
}

/*
 * 'tpcl_get_label_dimensions()' - Calculate label dimensions from printer settings
 */

bool tpcl_get_label_dimensions(ipp_t *printer_attrs, int print_width,
			       int print_height, int *label_pitch,
			       int *roll_width, pappl_job_t *job,
			       pappl_printer_t *printer)
{
	int label_gap = 50; // Default: 5mm
	int roll_margin = 10; // Default: 1mm

	// Get label gap from printer settings (0.1mm)
	label_gap = tpcl_get_int_option(printer_attrs, "label-gap", 50, job,
					printer);

	// Get roll margin from printer settings (0.1mm)
	roll_margin = tpcl_get_int_option(printer_attrs, "roll-margin", 10, job,
					  printer);

	// Calculate label pitch and roll width from retrieved values
	*label_pitch = print_height + label_gap;
	*roll_width = print_width + roll_margin;

	if (job)
		papplLogJob(
			job, PAPPL_LOGLEVEL_DEBUG,
			"Calculated label dimensions from page size: width=%d (0.1mm), height=%d (0.1mm), pitch=%d (0.1mm), roll=%d (0.1mm)",
			print_width, print_height, *label_pitch, *roll_width);
	else if (printer)
		papplLogPrinter(
			printer, PAPPL_LOGLEVEL_DEBUG,
			"Calculated label dimensions: width=%d (0.1mm), height=%d (0.1mm), pitch=%d (0.1mm), roll=%d (0.1mm)",
			print_width, print_height, *label_pitch, *roll_width);

	return true;
}

/*
 * 'tpcl_get_feed_adjustments()' - Get feed adjustment values from printer settings
 */

bool tpcl_get_feed_adjustments(ipp_t *printer_attrs, int *feed_adjustment,
			       int *cut_position_adjustment,
			       int *backfeed_adjustment, pappl_job_t *job,
			       pappl_printer_t *printer)
{
	// Get feed adjustment from printer settings (0.1mm)
	*feed_adjustment = tpcl_get_int_option(printer_attrs, "feed-adjustment",
					       0, job, printer);

	// Get cut position adjustment from printer settings (0.1mm)
	*cut_position_adjustment = tpcl_get_int_option(
		printer_attrs, "cut-position-adjustment", 0, job, printer);

	// Get backfeed adjustment from printer settings (0.1mm)
	*backfeed_adjustment = tpcl_get_int_option(
		printer_attrs, "backfeed-adjustment", 0, job, printer);

	// Return true if at least one adjustment is non-zero
	return (*feed_adjustment != 0 || *cut_position_adjustment != 0 ||
		*backfeed_adjustment != 0);
}
