# Toshiba TEC TPCL Printer Application

## 1. Overview

### 1.1 Introduction

This is a printer driver for Toshiba TEC label printers that use the TPCL (TEC Printer Command Language) version 2 protocol. It modernizes the original rastertotpcl driver to work with current **Linux** and **macOS** systems.

Instead of using PPDs, the driver emulates an IPP-compatible printer, allowing access over the network using standard printing protocols. This Application is built on the [PAPPL framework](https://github.com/michaelrsweet/pappl/) and provides a convenient web-based interface for adding printers and configuring settings.

### 1.2 Supported Printers

| Model | Resolution(s) | Thermal Transfer¹ |
|-------|---------------|-------------------|
| B-SA4G | 203dpi | Yes |
| B-SA4T | 300dpi | No |
| B-SX4 | 203dpi | Yes |
| B-SX5 | 203dpi, 300dpi | Yes |
| B-SX6 | 203dpi, 300dpi | Yes |
| B-SX8 | 203dpi, 300dpi | Yes |
| B-482 | 203dpi, 300dpi | Yes |
| B-572 | 203dpi, 300dpi | Yes |
| B-852R | 300dpi | Yes |
| B-SV4D | 203dpi | Yes |
| B-SV4T | 203dpi | Yes |
| B-EV4D-G | 203dpi | No |
| B-EV4D-T | 300dpi | No |
| B-EV4T-G | 203dpi | Yes |
| B-EV4T-T | 300dpi | Yes |

<sub>¹ All printers support Direct Thermal mode.</sub>

For detailed specifications including print speeds, resolutions, and supported media types, see [printer_properties.md](printer_properties.md). There is little variation between these printers other than resolutions, speeds, and accepted media types, so new printer models can be tested or added easily.

### 1.3 Tested Models

This driver has been tested primarily on a **Toshiba TEC B-EV4D-GS14** (203dpi variant). If you test with other models, encounter bugs, or can provide specifications for new devices, please [open an issue](https://github.com/yaourdt/rastertotpcl/issues). I will respond as soon as possible, though please note this project is maintained in my spare time.

## 2. Installation

### 2.1 DEB Package (Debian/Ubuntu)

DEB packages are available for Debian, Ubuntu, and derivative distributions. The DEB package includes a systemd service that is automatically enabled and started during installation.

**Installation:**

Download the appropriate DEB package and install it:

```bash
sudo dpkg -i tpcl-printer-app_*.deb
sudo apt-get install -f  # Install any missing dependencies
```

The service will start automatically after installation. You can verify it is running with:

```bash
sudo systemctl status tpcl-printer-app
```

### 2.2 RPM Package (RHEL/Fedora/openSUSE)

**Note:** RPM packages are currently unsigned.

RPM packages are available for RPM-based Linux distributions. The RPM package includes a systemd service that is automatically enabled and started during installation.

**Installation:**

Download the appropriate RPM package and install it:

```bash
sudo rpm -i tpcl-printer-app-*.rpm
```

The service will start automatically after installation. You can verify it is running with:

```bash
sudo systemctl status tpcl-printer-app
```

### 2.3 Manual Installation from Source

See [Building from Source](#7-building-from-source)

### 2.4 Uninstallation

Use your distros package manager or, for manual installations, `sudo make uninstall`. Configuration in `/var/lib/` and `/var/spool/` is preserved on uninstallation.

## 3. Usage Guide

### 3.1 Starting and Stopping the Server

**Starting manually:**

```bash
sudo ./bin/tpcl-printer-app server
```

**Using systemd (DEB/RPM installations):**

```bash
sudo systemctl start tpcl-printer-app
sudo systemctl stop tpcl-printer-app
sudo systemctl restart tpcl-printer-app
```

**Managing automatic startup at boot (DEB/RPM installations):**

The service is automatically enabled during package installation. To disable or re-enable:

```bash
sudo systemctl disable tpcl-printer-app
sudo systemctl enable tpcl-printer-app
```

**Checking service status (DEB/RPM installations):**

```bash
sudo systemctl status tpcl-printer-app
```

### 3.2 Web Interface Overview

Once the server is running, access the web interface at:

```
http://localhost:8000
```
**Note:** When adding a network printer, explicitly state the port (Toshiba default: 8000) as <ip>:<port> in the webinterface for the printer to be found. Alternatively, you may change the listening port on the printer to 9100 (PAPPL default) using the Toshiba settings tool.


### 3.3 Printer Identification Feature

The "Identify Printer" function in the web interface will feed a single blank label to help you identify which physical printer corresponds to the application. 

### 3.4 Printing Test Pages

The test page prints concentric boxes that are useful for verifying label positioning and making fine adjustments to margins and alignment. 

## 4. Configuration Reference

All configuration options are available through the web interface.

### 4.1 Media and Label Settings

#### Label Gap
Gap between labels in 0.1mm units.
- Range: 0-200
- Default: 50 (5.0mm)

#### Horizontal Roll Margin
Width difference between backing paper and label in 0.1mm units.
- Range: 0-300
- Default: 10 (1.0mm)

#### Sensor Type
Method for detecting labels:
- **None**: Continuous media without gaps
- **Reflective**: Uses reflective sensor (black mark detection)
- **Transmissive** (default): Uses transmissive sensor (gap detection)
- **Reflective Pre-Print**: Reflective sensor with pre-printed marks
- **Transmissive Pre-Print**: Transmissive sensor with pre-printed marks

### 4.2 Label Processing Options

#### Cut Label
Enable or disable label cutting if cutter is installed.
- **Non-Cut** (default): Do not cut labels
- **Cut**: Cut labels after printing

#### Cut Interval
Number of labels to print before cutting.
- Range: 0-100
- Default: 0 (no automatic cutting)

#### Feed Mode
Label feeding behavior if peel off device is installed:
- **Batch Mode** (default): Normal feeding for batch printing
- **Strip Mode (Backfeed with Sensor)**: Peel off with sensor feedback
- **Strip Mode (Backfeed without Sensor)**: Peel off without sensor
- **Partial Cut Mode**: Partial cutting

#### Feed on Label Size Change
Whether to feed a label when the label size changes.
- **Yes** (default): Feed label on size change
- **No**: Do not feed on size change
- Note: Should be enabled for most printers to avoid command errors

### 4.3 Fine-Tuning Adjustments

#### Feed Adjustment
Fine-tune label feed position in 0.1mm units.
- Range: -500 to 500
- Default: 0
- Negative values: move forward
- Positive values: move backward

#### Cut Position Adjustment
Fine-tune cut position if cutter is installed, in 0.1mm units.
- Range: -180 to 180
- Default: 0
- Negative values: move forward
- Positive values: move backward

#### Backfeed Adjustment
Fine-tune backfeed amount in 0.1mm units.
- Range: -99 to 99
- Default: 0
- Negative values: decrease backfeed
- Positive values: increase backfeed

#### Print Speed
Printing speed in arbitrary units (varies by model).
- Range and defaults vary by printer model
- See [printer_properties.md](printer_properties.md) for model-specific speed values and conversion to mm/second

#### Print Darkness
Darkness adjustment for print density.
- Range: -10 to 10
- Default: 0
- Negative values: lighter
- Positive values: darker

### 4.4 Advanced Graphics Settings

#### Data Transmission Mode
How graphics data is sent to the printer:
- **Nibble AND**: Nibble mode with AND logic
- **Hex AND**: Hex mode with AND logic
- **TOPIX** (default, recommended): Compressed format for best performance
- **Nibble OR**: Nibble mode with OR logic
- **Hex OR**: Hex mode with OR logic

#### Dithering Algorithm
Algorithm for converting grayscale images to black and white:
- **Threshold** (default): Simple threshold-based conversion
- **Bayer**: Bayer matrix dithering
- **Clustered**: Clustered dot dithering

#### Dithering Algorithm (Photo)
Separate dithering algorithm for photographic content:
- **Threshold** (default): Simple threshold-based conversion
- **Bayer**: Bayer matrix dithering for better photo reproduction
- **Clustered**: Clustered dot dithering for smoother gradients

#### Dithering Threshold
Threshold level when using threshold algorithm.
- Range: 0-255
- Default: 128
- Lower values: more black
- Higher values: more white

## 5. Troubleshooting and Debugging

### 5.1 Enabling Debug Logging

Run the application with debug logging enabled:

```bash
sudo ./bin/tpcl-printer-app server -o log-level=debug
```

For systemd installations, edit the service file to add the debug option.

### 5.2 Viewing Logs

**For manual runs with debug logging:**

Monitor the application logs in real-time:

```bash
sudo tail -f /tmp/pappl$(pidof tpcl-printer-app).log
sudo tail -f /tmp/rastertotpcl-nibble-dump-$(pidof tpcl-printer-app).out
```

**For systemd service:**

View logs using journalctl:

```bash
sudo journalctl -u tpcl-printer-app -f
```

### 5.3 Sending Direct TPCL Commands

You can send TPCL commands directly to the printer for testing or advanced operations. See the `/tests` directory for examples of direct command scripts.

This is useful for:
- Testing specific TPCL commands
- Debugging printer communication issues
- Implementing custom printer operations not available in the web interface
- Printing of large batches of labels from specialized software

## 6. File Locations Reference

### 6.1 Binary Locations

**Manual installation:**
- `/usr/local/bin/tpcl-printer-app`

**RPM package:**
- `/usr/bin/tpcl-printer-app`

**DEB package:**
- `/usr/bin/tpcl-printer-app`

### 6.2 Configuration Files

**tpcl-printer-app.conf** (stores server options, loaded in order):

- **User (Linux/BSD)**: `$XDG_CONFIG_HOME/tpcl-printer-app.conf` (if set) or `$HOME/.config/tpcl-printer-app.conf`
- **User (macOS)**: `$HOME/Library/Application Support/tpcl-printer-app.conf`
- **System (Linux/BSD)**: `/usr/local/etc/tpcl-printer-app.conf`, then `/etc/tpcl-printer-app.conf`
- **System (macOS)**: `/Library/Application Support/tpcl-printer-app.conf`
- **Snap**: `$SNAP_COMMON/tpcl-printer-app.conf`

### 6.3 State Files

**tpcl-printer-app.state** (stores system and printer configuration):

- **Root (Linux/BSD)**: `/var/lib/tpcl-printer-app.state`
- **User (Linux/BSD)**: `$XDG_CONFIG_HOME/tpcl-printer-app.state` (if set) or `$HOME/.config/tpcl-printer-app.state`
- **User (macOS)**: `$HOME/Library/Application Support/tpcl-printer-app.state`
- **Snap**: `$SNAP_COMMON/tpcl-printer-app.state`
- **Fallback**: `/tmp/tpcl-printer-app<uid>.state`

### 6.4 Spool Directories

Contains jobs and per-printer state files for label size tracking:

- **Root (Linux/BSD)**: `/var/spool/tpcl-printer-app/`
- **User (Linux/BSD)**: `$HOME/.config/tpcl-printer-app.d/`
- **User (macOS)**: `$HOME/Library/Application Support/tpcl-printer-app.d/`
- **Snap**: `$SNAP_COMMON/tpcl-printer-app.d/`
- **Fallback**: `/tmp/tpcl-printer-app<uid>.d/`

Per-printer state files are stored as `p<printer-id>-labelstate.state` (e.g., `p00001-labelstate.state`). These files track label dimensions to detect size changes.

## 7. Building from Source

### 7.1 Build Dependencies

#### Linux (openSUSE/SUSE)

```bash
sudo zypper install gcc gcc-c++ make pkg-config \
    cups-devel libppd libppd-devel cups-ddk \
    libjpeg8-devel libpng16-devel zlib-devel \
    libavahi-devel libopenssl-devel libusb-1_0-devel pam-devel \
    ImageMagick
```

#### Linux (Debian/Ubuntu)

```bash
sudo apt-get install build-essential gcc make pkg-config \
    libcups2-dev libjpeg-dev libpng-dev zlib1g-dev \
    libavahi-client-dev libssl-dev libusb-1.0-0-dev libpam0g-dev \
    imagemagick
```

#### macOS

```bash
brew install cups libjpeg libpng zlib openssl libusb imagemagick
```

### 7.2 Build Commands

**Full build** (clean rebuild including PAPPL framework):

```bash
make full
```

**Install after building:**

```bash
sudo make install
```

Running `make help` will show all available options and build flags.

### 7.3 Build System Notes

- **Build System Architecture**: The build system has clean separation between local development and package builds:
  - **Local builds** (Makefile): Uses git submodules, always builds PAPPL statically from source, version script generates only `src/version.h`
  - **Package builds** (debian/rules, .spec): Independent build logic, downloads PAPPL release tarball, version script updates package files
  - This separation ensures no git operations in package builds and prevents accidental modification of package files during development

- **SINGLE_ARCH Flag**: On macOS, use `SINGLE_ARCH=1` to force single-architecture builds and avoid multi-architecture linking errors with PAPPL

- **Translations**: Patched directly into PAPPL during the build process. See `/scripts` for details. This is why we statically link PAPPL and ship it with the binary.

- **Icons**: ImageMagick is used to convert icon images to C header files during the build.

- **Version Management**: The application version is automatically generated during build from git:
  - **On tagged commit**: Uses the tag (e.g., `v0.2.2` → `0.2.2`)
  - **Not on tagged commit**: Uses 8-character commit hash (e.g., `07c193ce`)
  - **With uncommitted changes**: Appends `-dirty` suffix (e.g., `07c193ce-dirty`)

## 8. Project Information

### 8.1 Project History

The original rastertotpcl driver converted CUPS Raster graphics along with a supported PPD file into TPCL graphics ready to be printed. It included support for the TPCL TOPIX compression algorithm but not for direct printing.

The original source can be found at [samlown/rastertotpcl](http://github.com/samlown/rastertotpcl).

### 8.2 License

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program. If not, see <http://www.gnu.org/licenses/>.

For certain builds, this binary comes prepacked with PAPPL, which is licensed under the Apache 2.0 license.

### 8.3 Credits and Authors

- **rastertotpcl** is based on the **rastertotec** driver written by Patrick Kong (SKE s.a.r.l)
- **rastertotec** is based on the **rastertolabel** driver included with the CUPS printing system by Easy Software Products
- Packaging of rastertotpcl and TOPIX compression was added by Sam Lown (www.samlown.com)
- Original MacOS adaptation by [Milverton](https://milverton.typepad.com/the-hairy-mouse/2011/10/print-to-toshiba-tec-b-ev4d-gs14-on-os-x.html)
- PAPPL migration, direct printing and current maintenance by Mark Dornbach (yaourdt)
