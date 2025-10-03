# Converting rastertotpcl to a PAPPL Printer Application

This document provides step-by-step instructions for converting the rastertotpcl CUPS driver into a modern PAPPL-based Printer Application that supports IPP (Internet Printing Protocol).

## Table of Contents
1. [Overview](#overview)
2. [Prerequisites](#prerequisites)
3. [Migration Approaches](#migration-approaches)
4. [Approach 1: Using pappl-retrofit (Recommended for Quick Migration)](#approach-1-using-pappl-retrofit-recommended-for-quick-migration)
5. [Approach 2: Native PAPPL Implementation (Recommended for Long-term)](#approach-2-native-pappl-implementation-recommended-for-long-term)
6. [Testing and Deployment](#testing-and-deployment)
7. [References](#references)

---

## Overview

### What are Printer Applications?

Printer Applications are the modern replacement for traditional CUPS printer drivers. They are:
- **Standalone services** that run in the background
- **IPP-compatible** - they emulate driverless IPP printers (like AirPrint)
- **Distribution-independent** - can be packaged as Snaps or other universal formats
- **Web-enabled** - provide a web interface for printer configuration
- **Sandboxed** - can run in isolated environments for security

### Why Migrate?

1. **CUPS 3.x compatibility** - Traditional PPD-based drivers are deprecated
2. **Broader platform support** - Works on mobile, IoT, and any IPP-capable device
3. **Easier deployment** - Single package works across all Linux distributions
4. **Better security** - Sandboxed execution with clear boundaries
5. **Modern architecture** - Aligns with IPP Everywhere™ and PWG standards

### What is PAPPL?

PAPPL (Printer Application Framework) is a C library by Michael R Sweet that provides:
- Core infrastructure for printer applications
- IPP service implementation
- Web interface framework
- Job management and spooling
- Raster processing utilities

---

## Prerequisites

### Development Environment

Install required dependencies:

**Ubuntu/Debian:**
```bash
sudo apt-get install \
    build-essential \
    git \
    libcups2-dev \
    libcupsimage2-dev \
    libjpeg-dev \
    libpng-dev \
    libusb-1.0-0-dev \
    libavahi-client-dev \
    libgnutls28-dev \
    libpam0g-dev \
    zlib1g-dev
```

**Fedora/RHEL:**
```bash
sudo dnf install \
    gcc \
    git \
    cups-devel \
    libjpeg-turbo-devel \
    libpng-devel \
    libusb1-devel \
    avahi-devel \
    gnutls-devel \
    pam-devel \
    zlib-devel
```

**macOS:**
```bash
brew install libusb libpng libjpeg
```

### Install PAPPL

```bash
# Clone PAPPL
git clone https://github.com/michaelrsweet/pappl.git
cd pappl

# Configure and build
./configure
make
sudo make install

# Verify installation
pkg-config --modversion pappl
```

---

## Migration Approaches

There are two main approaches to converting rastertotpcl to a PAPPL printer application:

### Approach 1: pappl-retrofit (Quick Migration)
- **Effort:** Low to Medium
- **Time:** 1-2 weeks
- **Pros:** Reuses existing PPD files and filter code with minimal changes
- **Cons:** Carries forward legacy design, less control over IPP features
- **Best for:** Quick migration, maintaining compatibility with existing configurations

### Approach 2: Native PAPPL (Long-term Solution)
- **Effort:** Medium to High
- **Time:** 3-6 weeks
- **Pros:** Clean implementation, full IPP features, better maintainability
- **Cons:** Requires significant code refactoring
- **Best for:** Long-term maintenance, full feature support, new development

---

## Approach 1: Using pappl-retrofit (Recommended for Quick Migration)

The pappl-retrofit library allows you to wrap existing PPD-based CUPS drivers with minimal code changes.

### Step 1: Install pappl-retrofit

```bash
# Clone the repository
git clone https://github.com/OpenPrinting/pappl-retrofit.git
cd pappl-retrofit

# Build and install
./autogen.sh
./configure
make
sudo make install
```

### Step 2: Create Application Structure

Create a new directory structure:

```bash
cd rastertotpcl
mkdir -p tpcl-printer-app
cd tpcl-printer-app
```

### Step 3: Write the Main Application File

Create `tpcl-printer-app.c`:

```c
#include <pappl-retrofit/pappl-retrofit.h>
#include <cups/cups.h>

// Driver data for Toshiba TEC TPCL printers
static pr_driver_t tpcl_drivers[] = {
  {
    .name = "Toshiba TEC TPCL",
    .description = "Toshiba TEC Label Printers (TPCL)",
    .devid = "MFG:Toshiba Tec;MDL:TPCL;",
    .extension = "tpcl-printer-app"
  },
  { NULL }
};

// System callback - sets up the printer application
static pappl_system_t *
system_cb(
    int           num_options,
    cups_option_t *options,
    void          *data)
{
  pappl_system_t *system;
  const char     *name = "TPCL Printer Application";
  const char     *hostname = NULL;
  int            port = 0;

  // Create system object
  system = papplSystemCreate(
      PAPPL_SOPTIONS_MULTI_QUEUE | PAPPL_SOPTIONS_WEB_INTERFACE |
      PAPPL_SOPTIONS_WEB_LOG | PAPPL_SOPTIONS_WEB_NETWORK |
      PAPPL_SOPTIONS_WEB_SECURITY | PAPPL_SOPTIONS_WEB_TLS,
      name,
      port,
      "_print,_universal",
      NULL,
      NULL,
      NULL,
      NULL,
      hostname,
      NULL
  );

  // Set footer HTML
  papplSystemSetFooterHTML(system,
      "Copyright &copy; 2025. Licensed under GPL v3.");

  return system;
}

int
main(int  argc, char *argv[])
{
  pr_printer_app_config_t config;

  // Configure the printer application
  memset(&config, 0, sizeof(config));

  config.system_cb = system_cb;
  config.drivers = tpcl_drivers;

  // Path to PPD files
  config.ppd_paths = "../src/ppd";

  // Path to filter
  config.filter_path = "../src/rastertotpcl";

  config.version = "1.0";
  config.web_if_footer = "Copyright &copy; 2025";

  // Run the application
  return prRetroMainloop(argc, argv, &config);
}
```

### Step 4: Create Makefile

Create `Makefile`:

```makefile
# Compiler and flags
CC = gcc
CFLAGS = -Wall -O2 $(shell pkg-config --cflags pappl-retrofit cups)
LDFLAGS = $(shell pkg-config --libs pappl-retrofit cups)

# Target executable
TARGET = tpcl-printer-app

# Source files
SOURCES = tpcl-printer-app.c
OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)/usr/bin
	install -m 755 $(TARGET) $(DESTDIR)/usr/bin/

.PHONY: all clean install
```

### Step 5: Build and Test

```bash
# Ensure rastertotpcl filter is built
cd ../src
make
cd ../tpcl-printer-app

# Build the printer application
make

# Run the application in test mode
sudo ./tpcl-printer-app server

# In another terminal, access the web interface
# Open browser to: http://localhost:8000
```

### Step 6: Create Snap Package (Optional)

Create `snapcraft.yaml`:

```yaml
name: tpcl-printer-app
version: '1.0'
summary: Toshiba TEC TPCL Printer Application
description: |
  Printer Application for Toshiba TEC label printers supporting TPCL v2.
  Supports TOPIX compression and multiple printer models.

grade: stable
confinement: strict
base: core22

apps:
  tpcl-printer-app:
    command: bin/tpcl-printer-app
    daemon: simple
    plugs:
      - network
      - network-bind
      - avahi-observe
      - raw-usb

parts:
  pappl:
    plugin: autotools
    source: https://github.com/michaelrsweet/pappl.git
    source-type: git
    build-packages:
      - libcups2-dev
      - libavahi-client-dev
      - libgnutls28-dev
      - libjpeg-dev
      - libpng-dev
      - libusb-1.0-0-dev
      - zlib1g-dev
    stage-packages:
      - libcups2
      - libavahi-client3
      - libgnutls30
      - libjpeg8
      - libpng16-16
      - libusb-1.0-0

  pappl-retrofit:
    plugin: autotools
    source: https://github.com/OpenPrinting/pappl-retrofit.git
    source-type: git
    after: [pappl]

  tpcl-printer-app:
    plugin: make
    source: .
    after: [pappl-retrofit]
```

Build the snap:
```bash
snapcraft
sudo snap install --dangerous tpcl-printer-app_1.0_amd64.snap
```

---

## Approach 2: Native PAPPL Implementation (Recommended for Long-term)

This approach involves rewriting the driver to use PAPPL's native callbacks, eliminating the need for PPD files and providing better integration.

### Step 1: Understand the Current Architecture

The current rastertotpcl filter has these key functions:
- `Setup()` - Initialize printer (send TPCL commands)
- `StartPage()` - Begin page (label dimensions, parameters)
- `OutputLine()` - Process each raster line (TOPIX compression or raw hex)
- `EndPage()` - Finish page (print command, eject)

These map directly to PAPPL callbacks.

### Step 2: Create Project Structure

```bash
mkdir -p tpcl-pappl-app/{src,media,docs}
cd tpcl-pappl-app
```

### Step 3: Define Media Sizes

Create `src/tpcl-media.h`:

```c
#ifndef TPCL_MEDIA_H
#define TPCL_MEDIA_H

#include <pappl/pappl.h>

// Media size definitions for Toshiba TEC label printers
typedef struct {
  const char *name;           // PPD name
  const char *description;    // Human-readable name
  int width_mm;               // Width in millimeters
  int length_mm;              // Length in millimeters
} tpcl_media_t;

static const tpcl_media_t tpcl_media_sizes[] = {
  {"w81h252",   "Address - 1 1/8 x 3 1/2\"",        28,  88},
  {"w101h252",  "Large Address - 1 4/10 x 3 1/2\"", 35,  88},
  {"w54h144",   "Return Address - 3/4 x 2\"",       19,  50},
  {"w167h288",  "Shipping Address - 2 5/16 x 4\"",  58, 101},
  {"w90h162",   "1.25x2.25\"",                      31,  57},
  {"w144h72",   "2.00x1.00\"",                      50,  25},
  {"w144h288",  "2.00x4.00\"",                      50, 101},
  {"w288h144",  "4.00x2.00\"",                     101,  50},
  {"w288h360",  "4.00x5.00\"",                     101, 127},
  {"w288h468",  "4.00x6.50\"",                     101, 165},
  {"w432h288",  "6.00x4.00\"",                     152, 101},
  {"w576h288",  "8.00x4.00\"",                     203, 101},
  {"w292h564",  "103mmx199mm (DHL Label)",         103, 199},
  {NULL, NULL, 0, 0}
};

#endif // TPCL_MEDIA_H
```

### Step 4: Create TPCL Driver Implementation

Create `src/tpcl-driver.c`:

```c
#include <pappl/pappl.h>
#include <string.h>
#include <math.h>
#include "tpcl-media.h"
#include "tpcl-driver.h"

// TPCL Graphics Modes
#define TEC_GMODE_TOPIX   1
#define TEC_GMODE_HEX_AND 3
#define TEC_GMODE_HEX_OR  5

// Job data structure
typedef struct {
  unsigned char *buffer;         // Current line buffer
  unsigned char *last_buffer;    // Previous line buffer
  unsigned char *comp_buffer;    // Compression buffer
  unsigned char *comp_buffer_ptr;
  int           comp_last_line;
  int           page;
  int           gmode;           // Graphics mode
  int           width;           // Line width in bytes
  int           height;          // Page height in lines
} tpcl_job_t;

// Forward declarations
static void tpcl_topix_compress(tpcl_job_t *job, int y);
static void tpcl_topix_output_buffer(pappl_job_t *job, tpcl_job_t *tpcl_job, int y);


// Driver callback - called when printer is created
bool tpcl_driver_cb(
    pappl_system_t        *system,
    const char            *driver_name,
    const char            *device_uri,
    const char            *device_id,
    pappl_pr_driver_data_t *driver_data,
    ipp_t                 **driver_attrs,
    void                  *data)
{
  int i;

  // Set basic driver information
  strncpy(driver_data->make_and_model, "Toshiba TEC TPCL Label Printer",
          sizeof(driver_data->make_and_model) - 1);

  driver_data->num_resolution = 3;
  driver_data->x_resolution[0] = 203;  // 8 dots/mm
  driver_data->y_resolution[0] = 203;
  driver_data->x_resolution[1] = 300;  // 12 dots/mm
  driver_data->y_resolution[1] = 300;
  driver_data->x_resolution[2] = 600;  // 24 dots/mm
  driver_data->y_resolution[2] = 600;
  driver_data->x_default = driver_data->y_default = 203;

  // Color support
  driver_data->color_supported = PAPPL_COLOR_MODE_MONOCHROME;
  driver_data->color_default = PAPPL_COLOR_MODE_MONOCHROME;

  // Raster types
  driver_data->raster_types = PAPPL_PWG_RASTER_TYPE_BLACK_1 |
                              PAPPL_PWG_RASTER_TYPE_BLACK_8;

  // Media sizes
  driver_data->num_media = 0;
  for (i = 0; tpcl_media_sizes[i].name != NULL &&
              driver_data->num_media < PAPPL_MAX_MEDIA; i++)
  {
    snprintf(driver_data->media[driver_data->num_media],
             sizeof(driver_data->media[0]),
             "%s_%dx%dmm",
             tpcl_media_sizes[i].name,
             tpcl_media_sizes[i].width_mm,
             tpcl_media_sizes[i].length_mm);

    driver_data->num_media++;
  }

  // Default media
  strncpy(driver_data->media_default, "w288h360_101x127mm",
          sizeof(driver_data->media_default) - 1);

  // Media ready (supported in all trays)
  driver_data->num_source = 1;
  strncpy(driver_data->source[0], "main", sizeof(driver_data->source[0]) - 1);

  // Print modes
  driver_data->num_type = 3;
  strncpy(driver_data->type[0], "thermal-transfer",
          sizeof(driver_data->type[0]) - 1);
  strncpy(driver_data->type[1], "direct-thermal",
          sizeof(driver_data->type[1]) - 1);
  strncpy(driver_data->type[2], "ribbon-saving",
          sizeof(driver_data->type[2]) - 1);

  // Print speeds (inches per second)
  driver_data->speed_supported[0] = 2;
  driver_data->speed_supported[1] = 10;
  driver_data->speed_default = 3;

  // Margins (none for label printers)
  driver_data->left_right = 0;
  driver_data->bottom_top = 0;

  // Duplex (not supported)
  driver_data->duplex = PAPPL_DUPLEX_NONE;

  // Printer callbacks
  driver_data->printfile_cb = NULL;  // Use raster callbacks
  driver_data->rendjob_cb = tpcl_rendjob;
  driver_data->rendpage_cb = tpcl_rendpage;
  driver_data->rstartjob_cb = tpcl_rstartjob;
  driver_data->rstartpage_cb = tpcl_rstartpage;
  driver_data->rwriteline_cb = tpcl_rwriteline;
  driver_data->status_cb = tpcl_status;

  return true;
}


// Start a job
bool tpcl_rstartjob(
    pappl_job_t    *job,
    pappl_pr_options_t *options,
    pappl_device_t *device)
{
  tpcl_job_t *tpcl_job;

  // Allocate job data
  tpcl_job = (tpcl_job_t *)calloc(1, sizeof(tpcl_job_t));
  papplJobSetData(job, tpcl_job);

  // Send reset command
  papplDevicePuts(device, "{WS|}\n");

  // Send printer setup commands (feed adjust, ribbon adjust, etc.)
  // Note: These would come from job options in a full implementation
  papplDevicePuts(device, "{AX;+0,+0,+0|}\n");  // Feed adjustments
  papplDevicePuts(device, "{RM;00|}");           // Ribbon motor

  return true;
}


// Start a page
bool tpcl_rstartpage(
    pappl_job_t        *job,
    pappl_pr_options_t *options,
    pappl_device_t     *device,
    unsigned           page)
{
  tpcl_job_t *tpcl_job = (tpcl_job_t *)papplJobGetData(job);
  char command[256];
  int length, width, labelpitch, labelgap;

  tpcl_job->page = page;
  tpcl_job->width = options->header.cupsBytesPerLine;
  tpcl_job->height = options->header.cupsHeight;

  // Calculate label dimensions (in 0.1mm units)
  width = (int)(options->media.size_width * 0.254);
  length = (int)(options->media.size_length * 0.254);
  labelgap = 30;  // 3mm default gap
  labelpitch = length + labelgap;

  // Send label size command
  snprintf(command, sizeof(command),
           "{D%04d,%04d,%04d,%04d|}\n",
           labelpitch, width, length, width + labelgap);
  papplDevicePuts(device, command);

  // Send temperature adjustment
  papplDevicePuts(device, "{AY;+00,0|}\n");

  // Clear image buffer
  papplDevicePuts(device, "{C|}\n");

  // Determine graphics mode (default to TOPIX)
  tpcl_job->gmode = TEC_GMODE_TOPIX;

  if (tpcl_job->gmode == TEC_GMODE_TOPIX) {
    // Allocate TOPIX compression buffers
    tpcl_job->buffer = malloc(tpcl_job->width);
    tpcl_job->last_buffer = malloc(tpcl_job->width);
    tpcl_job->comp_buffer = malloc(0xFFFF);

    memset(tpcl_job->last_buffer, 0, tpcl_job->width);
    memset(tpcl_job->comp_buffer, 0, 0xFFFF);

    tpcl_job->comp_buffer_ptr = tpcl_job->comp_buffer;
    tpcl_job->comp_last_line = 0;
  } else {
    // Allocate buffer for hex mode
    tpcl_job->buffer = malloc(tpcl_job->width);

    // Send graphics header for hex mode
    snprintf(command, sizeof(command),
             "{SG;0000,0000,%04d,%04d,%d,",
             tpcl_job->width * 8, tpcl_job->height, tpcl_job->gmode);
    papplDevicePuts(device, command);
  }

  return true;
}


// Write a line of raster data
bool tpcl_rwriteline(
    pappl_job_t         *job,
    pappl_pr_options_t  *options,
    pappl_device_t      *device,
    unsigned            y,
    const unsigned char *line)
{
  tpcl_job_t *tpcl_job = (tpcl_job_t *)papplJobGetData(job);

  // Copy line to buffer
  memcpy(tpcl_job->buffer, line, tpcl_job->width);

  if (tpcl_job->gmode == TEC_GMODE_TOPIX) {
    // Compress using TOPIX
    tpcl_topix_compress(tpcl_job, y);

    // Output buffer if getting full
    if ((tpcl_job->comp_buffer_ptr - tpcl_job->comp_buffer) >
        (0xFFFF - (tpcl_job->width + ((tpcl_job->width / 8) * 3)))) {
      tpcl_topix_output_buffer(job, tpcl_job, y);
      memset(tpcl_job->last_buffer, 0, tpcl_job->width);
    }
  } else {
    // Output raw hex data
    papplDeviceWrite(device, tpcl_job->buffer, tpcl_job->width);
  }

  return true;
}


// End a page
bool tpcl_rendpage(
    pappl_job_t        *job,
    pappl_pr_options_t *options,
    pappl_device_t     *device,
    unsigned           page)
{
  tpcl_job_t *tpcl_job = (tpcl_job_t *)papplJobGetData(job);
  char command[256];

  // Flush TOPIX buffer or close hex graphics
  if (tpcl_job->gmode == TEC_GMODE_TOPIX) {
    tpcl_topix_output_buffer(job, tpcl_job, 0);
  } else {
    papplDevicePuts(device, "|}\n");
  }

  // Send print command
  // Format: {XS;I,copies,cut+detect+mode+speed+media+mirror+status|}
  snprintf(command, sizeof(command),
           "{XS;I,%04d,%03d%dC3100|}\n",
           options->copies, 0, 0);  // Simplified parameters
  papplDevicePuts(device, command);

  // Send padding to avoid zero window error
  papplDevicePrintf(device, "%1024s", "");

  // Free buffers
  if (tpcl_job->buffer) {
    free(tpcl_job->buffer);
    tpcl_job->buffer = NULL;
  }
  if (tpcl_job->last_buffer) {
    free(tpcl_job->last_buffer);
    tpcl_job->last_buffer = NULL;
  }
  if (tpcl_job->comp_buffer) {
    free(tpcl_job->comp_buffer);
    tpcl_job->comp_buffer = NULL;
  }

  return true;
}


// End a job
bool tpcl_rendjob(
    pappl_job_t    *job,
    pappl_pr_options_t *options,
    pappl_device_t *device)
{
  tpcl_job_t *tpcl_job = (tpcl_job_t *)papplJobGetData(job);

  // Free job data
  if (tpcl_job) {
    free(tpcl_job);
    papplJobSetData(job, NULL);
  }

  return true;
}


// TOPIX compression algorithm
static void tpcl_topix_compress(tpcl_job_t *job, int y)
{
  int i, l1, l2, l3, max, width;
  unsigned char line[8][9][9] = {0};
  unsigned char cl1, cl2, cl3, xor;
  unsigned char *ptr;

  width = job->width;
  max = 8 * 9 * 9;

  // Perform XOR with previous line for differential compression
  cl1 = 0;
  i = 0;
  for (l1 = 0; l1 <= 7 && i < width; l1++) {
    cl2 = 0;
    for (l2 = 1; l2 <= 8 && i < width; l2++) {
      cl3 = 0;
      for (l3 = 1; l3 <= 8 && i < width; l3++, i++) {
        xor = job->buffer[i] ^ job->last_buffer[i];
        line[l1][l2][l3] = xor;
        if (xor > 0) {
          cl3 |= (1 << (8 - l3));
        }
      }
      line[l1][l2][0] = cl3;
      if (cl3 != 0)
        cl2 |= (1 << (8 - l2));
    }
    line[l1][0][0] = cl2;
    if (cl2 != 0)
      cl1 |= (1 << (7 - l1));
  }

  // Add CL1 to buffer
  *job->comp_buffer_ptr = cl1;
  job->comp_buffer_ptr++;

  // Copy non-zero bytes to compressed buffer
  if (cl1 > 0) {
    ptr = &line[0][0][0];
    for (i = 0; i < max; i++) {
      if (*ptr != 0) {
        *job->comp_buffer_ptr = *ptr;
        job->comp_buffer_ptr++;
      }
      ptr++;
    }
  }

  // Copy current line to last_buffer for next iteration
  memcpy(job->last_buffer, job->buffer, width);
}


// Output TOPIX compressed buffer
static void tpcl_topix_output_buffer(
    pappl_job_t *pappl_job,
    tpcl_job_t  *tpcl_job,
    int         y)
{
  pappl_device_t *device = papplJobGetDevice(pappl_job);
  unsigned short len, belen;
  char command[256];

  len = (unsigned short)(tpcl_job->comp_buffer_ptr - tpcl_job->comp_buffer);
  if (len == 0)
    return;

  // Convert to big-endian
  belen = (len << 8 | len >> 8);

  // Send SG command with compressed data
  snprintf(command, sizeof(command),
           "{SG;0000,%04d,%04d,%04d,%d,",
           tpcl_job->comp_last_line,
           tpcl_job->width * 8,
           300,
           tpcl_job->gmode);

  papplDevicePuts(device, command);
  papplDeviceWrite(device, &belen, 2);
  papplDeviceWrite(device, tpcl_job->comp_buffer, len);
  papplDevicePuts(device, "|}\n");

  if (y) tpcl_job->comp_last_line = y;

  // Reset buffer
  memset(tpcl_job->comp_buffer, 0, 0xFFFF);
  tpcl_job->comp_buffer_ptr = tpcl_job->comp_buffer;
}


// Status callback
bool tpcl_status(pappl_printer_t *printer)
{
  // In a full implementation, query printer status via device
  // For now, just return true (printer OK)
  return true;
}
```

### Step 5: Create Header File

Create `src/tpcl-driver.h`:

```c
#ifndef TPCL_DRIVER_H
#define TPCL_DRIVER_H

#include <pappl/pappl.h>

// Driver callback
bool tpcl_driver_cb(
    pappl_system_t        *system,
    const char            *driver_name,
    const char            *device_uri,
    const char            *device_id,
    pappl_pr_driver_data_t *driver_data,
    ipp_t                 **driver_attrs,
    void                  *data);

// Raster callbacks
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

bool tpcl_status(pappl_printer_t *printer);

#endif // TPCL_DRIVER_H
```

### Step 6: Create Main Application

Create `src/tpcl-printer-app.c`:

```c
#include <pappl/pappl.h>
#include "tpcl-driver.h"

// System callback
static pappl_system_t *
system_cb(
    int           num_options,
    cups_option_t *options,
    void          *data)
{
  pappl_system_t *system;

  system = papplSystemCreate(
      PAPPL_SOPTIONS_MULTI_QUEUE |
      PAPPL_SOPTIONS_WEB_INTERFACE |
      PAPPL_SOPTIONS_WEB_LOG |
      PAPPL_SOPTIONS_WEB_NETWORK |
      PAPPL_SOPTIONS_WEB_SECURITY |
      PAPPL_SOPTIONS_WEB_TLS,
      "Toshiba TEC TPCL Printer Application",
      8000,
      "_print,_universal",
      "/var/spool/tpcl-printer-app",
      "/var/log/tpcl-printer-app",
      "/var/run/tpcl-printer-app.pid",
      "/etc/tpcl-printer-app",
      cupsGetOption("hostname", num_options, options),
      cupsGetOption("port", num_options, options) ?
        atoi(cupsGetOption("port", num_options, options)) : 0
  );

  papplSystemAddListeners(system, NULL);
  papplSystemSetFooterHTML(system,
      "Copyright &copy; 2025 by Toshiba TEC. "
      "Provided under GNU GPL v3 license.");

  papplSystemSetSaveCallback(system,
      (pappl_save_cb_t)papplSystemSaveState,
      (void *)"/var/lib/tpcl-printer-app/state");

  // Add driver
  papplSystemAddDriver(system,
      "toshiba-tec-tpcl",
      "Toshiba TEC TPCL v2",
      NULL,
      tpcl_driver_cb,
      NULL);

  return system;
}


// Main entry point
int main(int argc, char *argv[])
{
  return papplMainloop(
      argc,
      argv,
      "1.0",
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      system_cb,
      NULL,
      "tpcl-printer-app"
  );
}
```

### Step 7: Create Build System

Create `Makefile`:

```makefile
CC = gcc
CFLAGS = -Wall -O2 -g $(shell pkg-config --cflags pappl)
LDFLAGS = $(shell pkg-config --libs pappl) -lm

TARGET = tpcl-printer-app
SOURCES = src/tpcl-printer-app.c src/tpcl-driver.c
OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)/usr/bin
	install -m 755 $(TARGET) $(DESTDIR)/usr/bin/
	install -d $(DESTDIR)/usr/share/tpcl-printer-app
	install -d $(DESTDIR)/var/lib/tpcl-printer-app
	install -d $(DESTDIR)/var/spool/tpcl-printer-app

.PHONY: all clean install
```

### Step 8: Build and Test

```bash
# Build
make

# Run in test mode
sudo ./tpcl-printer-app server -o log-level=debug

# In another terminal, add a printer
./tpcl-printer-app add -d "Test TPCL Printer" -v usb://Toshiba%20TEC/B-SX4

# Access web interface
# Open browser to: http://localhost:8000
```

---

## Testing and Deployment

### Testing the Application

1. **Test with simulated device:**
```bash
# Create a test file device
sudo ./tpcl-printer-app server -o server-port=8000
```

2. **Print test label:**
```bash
# Create a simple test image
convert -size 288x360 -background white -fill black \
        -pointsize 72 -gravity center label:"TEST" test.png

# Print via IPP
ipptool -tv ipp://localhost:8000/ipp/print/test test.png
```

3. **Check logs:**
```bash
tail -f /var/log/tpcl-printer-app/error_log
```

### Deployment Options

#### Option 1: System Service

Create `/etc/systemd/system/tpcl-printer-app.service`:

```ini
[Unit]
Description=Toshiba TEC TPCL Printer Application
After=network.target

[Service]
Type=simple
ExecStart=/usr/bin/tpcl-printer-app server
Restart=on-failure
User=lp
Group=lp

[Install]
WantedBy=multi-user.target
```

Enable and start:
```bash
sudo systemctl enable tpcl-printer-app
sudo systemctl start tpcl-printer-app
```

#### Option 2: Snap Package

Build and install as shown in Approach 1, Step 6.

#### Option 3: Docker Container

Create `Dockerfile`:

```dockerfile
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential \
    git \
    libcups2-dev \
    libavahi-client-dev \
    libgnutls28-dev \
    libjpeg-dev \
    libpng-dev \
    libusb-1.0-0-dev \
    && rm -rf /var/lib/apt/lists/*

# Install PAPPL
WORKDIR /tmp
RUN git clone https://github.com/michaelrsweet/pappl.git && \
    cd pappl && \
    ./configure && \
    make && \
    make install

# Copy and build application
WORKDIR /app
COPY . .
RUN make

EXPOSE 8000
CMD ["./tpcl-printer-app", "server", "-o", "log-level=info"]
```

Build and run:
```bash
docker build -t tpcl-printer-app .
docker run -d -p 8000:8000 --name tpcl-printer tpcl-printer-app
```

---

## References

### Documentation
- **PAPPL Documentation:** https://www.msweet.org/pappl/pappl.html
- **PAPPL GitHub:** https://github.com/michaelrsweet/pappl
- **OpenPrinting Guides:** https://openprinting.github.io/documentation/
- **pappl-retrofit:** https://github.com/OpenPrinting/pappl-retrofit

### Example Applications
- **HP Printer App:** https://github.com/michaelrsweet/hp-printer-app
- **PostScript Printer App:** https://github.com/OpenPrinting/ps-printer-app
- **Ghostscript Printer App:** https://github.com/OpenPrinting/ghostscript-printer-app

### Specifications
- **IPP Everywhere:** https://www.pwg.org/ipp/everywhere.html
- **PWG Raster Format:** https://ftp.pwg.org/pub/pwg/candidates/cs-ippraster10-20120420-5102.4.pdf
- **TPCL Documentation:** Toshiba TEC TPCL Reference Manual

---

## Next Steps

1. **Choose your approach** - Quick migration (pappl-retrofit) or native PAPPL
2. **Set up development environment** - Install PAPPL and dependencies
3. **Start with basic implementation** - Get a minimal printer application running
4. **Add TOPIX compression** - Port the compression algorithm
5. **Test thoroughly** - Verify output with actual printers
6. **Package for distribution** - Create Snap or system packages
7. **Update documentation** - Document IPP-specific features and options

---

## Support

For questions and issues:
- **PAPPL Issues:** https://github.com/michaelrsweet/pappl/issues
- **OpenPrinting Forum:** https://discourse.ubuntu.com/c/printing/
- **CUPS Development:** https://github.com/OpenPrinting/cups

Good luck with your migration!
