# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a CUPS raster driver for Toshiba TEC label printers supporting TPCL (TEC Printer Command Language) version 2. It converts CUPS Raster graphics into TPCL-formatted print jobs with TOPIX compression.

The driver consists of:
- **rastertotpcl** - Main C filter that converts CUPS raster data to TPCL commands
- **PPD files** - Generated from tectpcl2.drv for various Toshiba TEC printer models
- **TOPIX compression** - Custom compression algorithm for efficient print data transmission

## Build Commands

### Prerequisites
**Linux:**
```bash
sudo apt-get install build-essential libcups2-dev cups-ppdc
```

**macOS:**
Install Xcode from the App Store.

### Building
```bash
make                 # Compile rastertotpcl filter and generate PPD files
make clean           # Clean build artifacts
```

### Installation
```bash
sudo make install    # Install filter and PPD files to CUPS directories
sudo make uninstall  # Remove installed driver components
```

## Architecture

### Main Filter (src/rastertotpcl.c)

The filter processes raster data through several stages:

1. **Setup()** - Initialize printer with TPCL commands including:
   - Feed adjustment parameters (AX command)
   - Ribbon motor setup (RM command)
   - Reset command (WS command)

2. **StartPage()** - For each page:
   - Calculate label dimensions and gaps (D command)
   - Set temperature adjustments (AY command)
   - Clear image buffer (C command)
   - Choose graphics mode: TOPIX compression (mode 1) or raw hex (modes 3/5)

3. **OutputLine()** - Process each raster line:
   - If TOPIX mode: calls TOPIXCompress() for differential compression
   - If hex mode: outputs raw 8-bit graphics data

4. **EndPage()** - Finish page with:
   - Output remaining compressed data
   - Set print parameters (XS command): copies, media tracking, speed, thermal mode
   - Optional cutter activation (IB command)

### TOPIX Compression

TOPIX is a 3-level hierarchical compression algorithm implemented in TOPIXCompress():
- Performs XOR with previous line to find changed bytes
- Builds 3-tier index structure (8×9×9 bytes)
- Only transmits changed data and index bytes
- Outputs via SG command with big-endian length prefix

The compression buffer (0xFFFF bytes) is flushed when near capacity to avoid overruns.

### PPD Generation (src/tectpcl2.drv)

The .drv file defines printer configurations with:
- Media sizes in labelmedia.h (from address labels to 103×199mm sheets)
- Printer-specific parameters: resolution, speed, ribbon settings
- UI options: graphics mode, media tracking, print speed, feed adjustments
- Localization support via .po files in po/ directory

PPDs are generated using `ppdc` compiler on Linux, or pre-compiled on macOS.

## Platform-Specific Notes

**macOS:** System Integrity Protection prevents writing to standard CUPS data directories. PPD files install to `$(cups-config --serverroot)/ppd/` instead.

**Linux:** Driver files install to standard CUPS directories retrieved via `cups-config`.

## Key Files

- `src/rastertotpcl.c` - Main filter implementation (950 lines)
- `src/tectpcl2.drv` - PPD driver definitions
- `src/labelmedia.h` - Media size definitions for label printers
- `src/Makefile` - Build configuration with platform detection
