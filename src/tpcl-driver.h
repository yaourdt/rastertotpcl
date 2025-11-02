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
 * Printer model speed constants
 */

#define TPCL_PRNT_SPEED 3 // Min, Default, Max


/*
 * Printer properties structure
 */

typedef struct {
  const char             *name;                        // Name, equal to name in 'tpcl_drivers[]'
  int                    print_min_width;              // Minimum label width in points (1 point = 1/72 inch)
  int                    print_min_height;             // Minimum label length in points
  int                    print_max_width;              // Maximum label width in points
  int                    print_max_height;             // Maximum label length in points
  bool                   resolution_203;               // Printer offers 203 dpi resolution if true
  bool                   resolution_300;               // Printer offers 300 dpi resolution if true
  bool                   thermal_transfer;             // Supports thermal transfer media if true
  bool                   thermal_transfer_with_ribbon; // Thermal transfer with ribbon support if true
  int                    print_speeds[TPCL_PRNT_SPEED];// Print speed settings (min, default, max)
} tpcl_printer_t;


/*
 * Driver information structure
 */

extern const pappl_pr_driver_t tpcl_drivers[];
extern const int tpcl_drivers_count;
extern const tpcl_printer_t tpcl_printer_properties[];


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


#endif // TPCL_DRIVER_H
