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
 * System callback - creates and configures the printer application system
 */

static pappl_system_t *
system_cb(
    int           num_options,
    cups_option_t *options,
    void          *data)
{
  pappl_system_t *system;
  const char     *hostname = NULL;
  int            port = 0;

  // Get hostname and port from options if specified
  hostname = cupsGetOption("hostname", num_options, options);
  if (cupsGetOption("port", num_options, options))
    port = atoi(cupsGetOption("port", num_options, options));

  // Create the system object
  // papplSystemCreate(options, name, port, subtypes, spooldir, logfile, loglevel, auth_service, tls_only)
  system = papplSystemCreate(
      PAPPL_SOPTIONS_MULTI_QUEUE |
      PAPPL_SOPTIONS_WEB_INTERFACE |
      PAPPL_SOPTIONS_WEB_LOG |
      PAPPL_SOPTIONS_WEB_NETWORK |
      PAPPL_SOPTIONS_WEB_SECURITY |
      PAPPL_SOPTIONS_WEB_TLS,
      "Toshiba TEC TPCL Printer Application",
      port,
      "_print,_universal",
      cupsGetOption("spool-directory", num_options, options),
      cupsGetOption("log-file", num_options, options),
      PAPPL_LOGLEVEL_INFO,  // log level
      cupsGetOption("auth-service", num_options, options),
      false  // tls_only
  );

  // Set footer HTML for web interface
  papplSystemSetFooterHTML(system,
      "Copyright &copy; 2020-2025 by Mark Dornbach. "
      "Licensed under GNU GPL v3.");

  // Set save callback for persistent state
  papplSystemSetSaveCallback(system,
      (pappl_save_cb_t)papplSystemSaveState,
      (void *)cupsGetOption("state-file", num_options, options));

  // Add network listeners
  papplSystemAddListeners(system, NULL);

  return system;
}


/*
 * Main entry point for the printer application
 */

int
main(int  argc,
     char *argv[])
{
  // papplMainloop(argc, argv, version, footer_html, num_drivers, drivers, autoadd_cb, driver_cb, subcmd_name, subcmd_cb, system_cb, usage_cb, data)
  return papplMainloop(
      argc,
      argv,
      "0.2.0",                            // Version
      NULL,                               // HTML footer
      1,                                  // Number of drivers
      (pappl_pr_driver_t *)tpcl_drivers,  // Driver information array
      NULL,                               // Auto-add callback
      tpcl_driver_cb,                     // Driver callback
      NULL,                               // Subcommand name
      NULL,                               // Subcommand callback
      system_cb,                          // System callback
      NULL,                               // Usage callback
      NULL                                // Callback data
  );
}
