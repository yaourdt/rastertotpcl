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
#include <unistd.h>
#include <string.h>


/* State files are managed by PAPPL (platform-specific paths) */
#define STATE_PATH_MAX 512


/* State file field names */
#define STATE_FIELD_WIDTH  "last_print_width"
#define STATE_FIELD_HEIGHT "last_print_height"
#define STATE_FIELD_GAP    "last_label_gap"
#define STATE_FIELD_MARGIN "last_roll_margin"


/*
 * Private state structure
 */

typedef struct {
  int  last_print_width;
  int  last_print_height;
  int  last_label_gap;
  int  last_roll_margin;
} tpcl_printer_state_t;


/*
 * Private helper functions
 */

static int  open_state_file(pappl_printer_t *printer, const char *mode, char *filepath, size_t filepath_size);
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
  char filepath[STATE_PATH_MAX];
  int fd;

  fd = open_state_file(printer, "x", filepath, sizeof(filepath));

  if (fd >= 0)
    close(fd);

  papplLogPrinter(printer, fd >= 0 ? PAPPL_LOGLEVEL_INFO : PAPPL_LOGLEVEL_DEBUG,
                  fd >= 0 ? "Deleted state file: %s" : "No state file to delete",
                  filepath);
}


/*
 * 'open_state_file()' - Open state file using PAPPL's file management
 */

static int
open_state_file(
  pappl_printer_t *printer,
  const char      *mode,
  char            *filepath,
  size_t          filepath_size
)
{
  char path[STATE_PATH_MAX];
  int fd;

  fd = papplPrinterOpenFile(printer, path, sizeof(path), NULL, "labelstate", "state", mode);

  if (filepath && filepath_size > 0)
    snprintf(filepath, filepath_size, "%s", path);

  return fd;
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
  char filepath[STATE_PATH_MAX];
  int  fd;
  FILE *fp;
  char line[STATE_PATH_MAX];
  int loaded_width = -1, loaded_height = -1, loaded_gap = -1, loaded_margin = -1;

  // Open state file using PAPPL
  fd = open_state_file(printer, "r", filepath, sizeof(filepath));
  if (fd < 0)
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_DEBUG, "No previous state file found");
    return false;
  }

  // Convert file descriptor to FILE* for easier parsing
  fp = fdopen(fd, "r");
  if (!fp)
  {
    close(fd);
    papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Failed to fdopen state file");
    return false;
  }

  // Read state from file
  while (fgets(line, sizeof(line), fp))
  {
    // Remove trailing newline
    line[strcspn(line, "\n")] = '\0';

    if (sscanf(line, STATE_FIELD_WIDTH "=%d", &loaded_width) == 1)
      continue;
    if (sscanf(line, STATE_FIELD_HEIGHT "=%d", &loaded_height) == 1)
      continue;
    if (sscanf(line, STATE_FIELD_GAP "=%d", &loaded_gap) == 1)
      continue;
    if (sscanf(line, STATE_FIELD_MARGIN "=%d", &loaded_margin) == 1)
      continue;
  }

  fclose(fp);  // This also closes fd

  // Validate that we loaded all required fields
  if (loaded_width < 0 || loaded_height < 0 || loaded_gap < 0 || loaded_margin < 0)
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_WARN, "Failed to load complete state file, ignoring");
    return false;
  }

  // Copy loaded state
  state->last_print_width = loaded_width;
  state->last_print_height = loaded_height;
  state->last_label_gap = loaded_gap;
  state->last_roll_margin = loaded_margin;

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
  char filepath[STATE_PATH_MAX];
  int  fd;
  FILE *fp;

  // Open file for writing using PAPPL (creates directories automatically)
  fd = open_state_file(printer, "w", filepath, sizeof(filepath));
  if (fd < 0)
  {
    papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Failed to open state file for writing");
    return false;
  }

  // Convert file descriptor to FILE* for easier writing
  fp = fdopen(fd, "w");
  if (!fp)
  {
    close(fd);
    papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Failed to fdopen state file");
    return false;
  }

  // Write state to file
  if (fprintf(fp, "# TPCL Printer State File\n") < 0 ||
      fprintf(fp, "# Auto-generated - do not edit manually\n") < 0 ||
      fprintf(fp, STATE_FIELD_WIDTH "=%d\n", state->last_print_width) < 0 ||
      fprintf(fp, STATE_FIELD_HEIGHT "=%d\n", state->last_print_height) < 0 ||
      fprintf(fp, STATE_FIELD_GAP "=%d\n", state->last_label_gap) < 0 ||
      fprintf(fp, STATE_FIELD_MARGIN "=%d\n", state->last_roll_margin) < 0)
  {
    fclose(fp);
    papplLogPrinter(printer, PAPPL_LOGLEVEL_ERROR, "Failed to write state file");
    return false;
  }

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
  return (state->last_print_width != w ||
          state->last_print_height != h ||
          state->last_label_gap != g ||
          state->last_roll_margin != m);
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
