/*
 * Toshiba TEC TPCL Printer Application
 *
 * A PAPPL-based printer application for Toshiba TEC label printers
 * supporting TPCL (TEC Printer Command Language) version 2.
 *
 * Copyright © 2020-2025 by Mark Dornbach
 * Copyright © 2010 by Sam Lown
 * Copyright © 2009 by Patrick Kong
 * Copyright © 2001-2007 by Easy Software Products
 *
 * Licensed under GNU GPL v3.
 */

#include <pappl/pappl.h>
#include "tpcl-driver.h"


/*
 * Footer HTML for web interface
 */
const char* footer = "Copyright &copy; 2001-2025 by Mark Dornbach and other authors. "
  "Licensed under GNU GPL v3.";


/*
 * Main entry point for the printer application
 */

int
main(int  argc,
     char *argv[])
{
  // papplMainloop(argc, argv, version, footer_html, num_drivers, drivers, autoadd_cb, driver_cb, subcmd_name, subcmd_cb, system_cb, usage_cb, data)
  return papplMainloop(
      argc,                               // Number of command line arguments
      argv,                               // Command line arguments
      "0.2.0",                            // Version number
      footer,                             // Footer HTML or NULL for none
      tpcl_drivers_count,                 // Number of drivers
      (pappl_pr_driver_t *)tpcl_drivers,  // Driver information array
      NULL,                               // Auto-add callback or NULL for none
      tpcl_driver_cb,                     // Driver callback to configure a printer
      NULL,                               // Subcommand name or NULL for none
      NULL,                               // Subcommand callback or NULL for none
      NULL,                               // System callback or NULL for default
      NULL,                               // Usage callbackor NULL for default
      NULL                                // Application specific callback data
  );
}
