/*
 * TPCL State Management Header
 *
 * State persistence for Toshiba TEC label printers.
 * Tracks label dimensions across jobs to detect size changes.
 *
 * Copyright © 2025 by Mark Dornbach
 *
 * Licensed under GNU GPL v3.
 */

#ifndef TPCL_STATE_H
#define TPCL_STATE_H

#include <pappl/pappl.h>

/*
 * 'tpcl_state_check_and_update()' - Check if label dimensions changed and update state
 *
 * Reads previous state from file, compares with current dimensions,
 * updates state file if changed, and returns whether change occurred.
 * Automatically handles file creation, updates, and logging.
 *
 * Parameters:
 *   printer      - Printer instance
 *   print_width  - Effective print width in 0.1mm units
 *   print_height - Effective print height in 0.1mm units
 *   label_gap    - Gap between labels in 0.1mm units
 *   roll_margin  - Roll margin (backing paper width difference) in 0.1mm units
 *   job          - Job context for logging (can be NULL for printer-level operations)
 *
 * Returns:
 *   true  - Dimensions changed from previous state (or first run)
 *   false - Dimensions unchanged from previous state
 */

bool tpcl_state_check_and_update(pappl_printer_t *printer, int print_width,
				 int print_height, int label_gap,
				 int roll_margin, pappl_job_t *job);

/*
 * 'tpcl_state_delete()' - Delete state file when printer is deleted
 *
 * Removes the persistent state file for the specified printer.
 * Should be called from the printer deletion callback.
 *
 * Parameters:
 *   printer - Printer instance being deleted
 */

void tpcl_state_delete(pappl_printer_t *printer);

#endif // TPCL_STATE_H
