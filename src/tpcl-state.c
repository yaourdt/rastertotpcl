/*
 * TPCL State Management Implementation
 *
 * State persistence for Toshiba TEC label printers.
 * Tracks label dimensions across jobs to detect size changes.
 *
 * Copyright © 2025 by Mark Dornbach
 *
 * Licensed under GNU GPL v3.
 */

#include "tpcl-state.h"
#include <sys/stat.h>
#include <errno.h>
#include <string.h>


/*
 * Platform-specific state file directory
 */

#ifdef __APPLE__
  #define TPCL_STATE_DIR "/Library/Application Support/tpcl-printer-app"
#else
  #define TPCL_STATE_DIR "/usr/local/etc/tpcl-printer-app"
#endif


/*
 * Private state structure
 */

typedef struct {
  int  last_print_width;
  int  last_print_height;
  int  last_label_gap;
  int  last_roll_margin;
  bool initialized;
} tpcl_printer_state_t;


/*
 * Private helper functions
 */

static bool get_state_file_path(pappl_printer_t *printer, char *filepath, size_t filepath_size);
static bool ensure_state_directory(void);
static bool load_state_from_file(pappl_printer_t *printer, tpcl_printer_state_t *state);
static bool save_state_to_file(pappl_printer_t *printer, const tpcl_printer_state_t *state);
static bool state_has_changed(const tpcl_printer_state_t *state, int w, int h, int g, int m);
static void log_state_change(const tpcl_printer_state_t *old, int w, int h, int g, int m, pappl_job_t *job, pappl_printer_t *printer);


/*
 * 'tpcl_state_check_and_update()' - Check if label dimensions changed and update state
 */

bool
tpcl_state_check_and_update(
  pappl_printer_t *printer,
  int             print_width,
  int             print_height,
  int             label_gap,
  int             roll_margin,
  pappl_job_t     *job
)
{
  tpcl_printer_state_t state = {0};
  bool changed = false;

  // Load previous state (returns false if no file exists - that's OK, first run)
  bool had_state = load_state_from_file(printer, &state);

  if (!had_state)
  {
    // First time - no previous state
    changed = true;
    if (job)
      papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, "No previous label dimensions found, this is likely the first job");
    else
      papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "No previous label dimensions found");
  }
  else
  {
    // Check if dimensions changed
    changed = state_has_changed(&state, print_width, print_height, label_gap, roll_margin);

    if (changed)
    {
      log_state_change(&state, print_width, print_height, label_gap, roll_margin, job, printer);
    }
  }

  // Only update state file if dimensions changed
  if (changed)
  {
    state.last_print_width = print_width;
    state.last_print_height = print_height;
    state.last_label_gap = label_gap;
    state.last_roll_margin = roll_margin;
    state.initialized = true;

    save_state_to_file(printer, &state);
  }

  return changed;
}


/*
 * 'tpcl_state_delete()' - Delete state file when printer is deleted
 */

void
tpcl_state_delete(
  pappl_printer_t *printer
)
{
  char filepath[512];

  if (get_state_file_path(printer, filepath, sizeof(filepath)))
  {
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
  }
}


/*
 * Private helper implementations
 */


/*
 * 'get_state_file_path()' - Construct state file path for printer
 */

static bool
get_state_file_path(
  pappl_printer_t *printer,
  char            *filepath,
  size_t          filepath_size
)
{
  const char *printer_name = papplPrinterGetName(printer);

  if (!printer_name)
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Cannot get printer name for state file");
    return false;
  }

  snprintf(filepath, filepath_size, "%s/%s.state", TPCL_STATE_DIR, printer_name);
  return true;
}


/*
 * 'ensure_state_directory()' - Create state directory if it doesn't exist
 */

static bool
ensure_state_directory(void)
{
  if (mkdir(TPCL_STATE_DIR, 0755) != 0 && errno != EEXIST)
  {
    return false;
  }
  return true;
}


/*
 * 'load_state_from_file()' - Load printer state from file
 */

static bool
load_state_from_file(
  pappl_printer_t      *printer,
  tpcl_printer_state_t *state
)
{
  char filepath[512];
  FILE *fp;
  char line[512];
  int loaded_width = -1, loaded_height = -1, loaded_gap = -1, loaded_margin = -1;

  // Get file path
  if (!get_state_file_path(printer, filepath, sizeof(filepath)))
    return false;

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

  papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Loaded state from %s: width=%d, height=%d, gap=%d, margin=%d", filepath, loaded_width, loaded_height, loaded_gap, loaded_margin);

  return true;
}


/*
 * 'save_state_to_file()' - Save printer state to file
 */

static bool
save_state_to_file(
  pappl_printer_t            *printer,
  const tpcl_printer_state_t *state
)
{
  char filepath[512];
  FILE *fp;

  // Create directory if it doesn't exist
  if (!ensure_state_directory())
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Failed to create directory %s: %s", TPCL_STATE_DIR, strerror(errno));
    return false;
  }

  // Get file path
  if (!get_state_file_path(printer, filepath, sizeof(filepath)))
    return false;

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

  papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "Saved state to %s: width=%d, height=%d, gap=%d, margin=%d", filepath, state->last_print_width, state->last_print_height, state->last_label_gap, state->last_roll_margin);

  return true;
}


/*
 * 'state_has_changed()' - Check if state dimensions have changed
 */

static bool
state_has_changed(
  const tpcl_printer_state_t *state,
  int                        w,
  int                        h,
  int                        g,
  int                        m
)
{
  if (!state->initialized)
    return false;

  return
  (
    state->last_print_width != w ||
    state->last_print_height != h ||
    state->last_label_gap != g ||
    state->last_roll_margin != m
  );
}


/*
 * 'log_state_change()' - Log state change with old and new values
 */

static void
log_state_change(
  const tpcl_printer_state_t *old,
  int                        w,
  int                        h,
  int                        g,
  int                        m,
  pappl_job_t                *job,
  pappl_printer_t            *printer
)
{
  const char *fmt = "Label size changed: old(%d×%d, %d, %d) → new(%d×%d, %d, %d) [width×height, gap, margin in 0.1mm]";

  if (job)
    papplLogJob(job, PAPPL_LOGLEVEL_DEBUG, fmt, old->last_print_width, old->last_print_height, old->last_label_gap, old->last_roll_margin, w, h, g, m);
  else if (printer)
    papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, fmt, old->last_print_width, old->last_print_height, old->last_label_gap, old->last_roll_margin, w, h, g, m);
}
