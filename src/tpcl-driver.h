/*
 * TPCL Driver Header
 *
 * Header file for Toshiba TEC TPCL printer driver callbacks.
 *
 * Copyright © 2025 by Mark Dornbach
 *
 * Licensed under GNU GPL v3.
 */

#ifndef TPCL_DRIVER_H
#define TPCL_DRIVER_H

#include <pappl/pappl.h>


/*
 * Driver information structure
 */

extern const pappl_pr_driver_t tpcl_drivers[];


/*
 * Main driver callback - configures printer capabilities
 */

bool tpcl_driver_cb(
    pappl_system_t         *system,
    const char             *driver_name,
    const char             *device_uri,
    const char             *device_id,
    pappl_pr_driver_data_t *driver_data,
    ipp_t                  **driver_attrs,
    void                   *data);


/*
 * Raster printing callbacks
 */

bool tpcl_rstartjob(
    pappl_job_t        *job,
    pappl_pr_options_t *options,
    pappl_device_t     *device);

bool tpcl_rstartpage(
    pappl_job_t        *job,
    pappl_pr_options_t *options,
    pappl_device_t     *device,
    unsigned           page);

bool tpcl_rwriteline(
    pappl_job_t         *job,
    pappl_pr_options_t  *options,
    pappl_device_t      *device,
    unsigned            y,
    const unsigned char *line);

bool tpcl_rendpage(
    pappl_job_t        *job,
    pappl_pr_options_t *options,
    pappl_device_t     *device,
    unsigned           page);

bool tpcl_rendjob(
    pappl_job_t        *job,
    pappl_pr_options_t *options,
    pappl_device_t     *device);


/*
 * Status callback
 */

bool tpcl_status(
    pappl_printer_t *printer);


#endif // TPCL_DRIVER_H
