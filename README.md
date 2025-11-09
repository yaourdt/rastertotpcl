# Toshiba TEC TPCL Printer Application

## Introduction

This is a printer driver for Toshiba TEC label printers that use the TPCL (TEC Printer Command Language) version 2 protocol. It modernizes the original rastertotpcl driver to work with current **Linux** and **macOS** systems.

Modern operating systems (or more precisely modern versions of CUPS) have moved away from the old PPD-based printing system. This driver has been updated to work seamlessly with these newer systems, allowing the continued use of Toshiba TEC label printers without compatibility issues.

In order to do so, the driver emulates an IPP-compatible printer and provides a convenient web-based interface for configuring printer settings.

This driver has been tested primarily on a **Toshiba TEC B-EV4D-GS14**. If you test with other models, encounter bugs, or can provide specifications for new devices, please [open an issue](https://github.com/yaourdt/rastertotpcl/issues). I will respond as soon as possible, though please note this project is maintained in my spare time.

## Supported Printers

The following Toshiba TEC label printers are supported. For detailed specifications including print speeds, resolutions, and supported media types, see [printer_properties.md](printer_properties.md).

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
| B-EV4D-GS14 | 203dpi | Yes |
| B-EV4T-GS14 | 203dpi | Yes |

<sub>¹ All printers support Direct Thermal mode.</sub>

There is little variation between these printers other than resolutions, speeds, and accepted media types, so new printer models can be tested or added easily.

## Starting the Server

### Basic Usage

To start the printer application server:

```bash
sudo ./bin/tpcl-printer-app server
```

View available command-line options:

```bash
./bin/tpcl-printer-app --help
```

## Web Interface

Once the server is running, access the web interface at:

```
http://localhost:8000
```

Or if running on a remote machine:

```
http://<hostname>:8000
```

### Web Interface Options

The web interface provides the following configuration options:

#### Media and Label Settings

- **Label Gap**: Gap between labels in 0.1mm units (0-200, default: 50)
- **Horizontal Roll Margin**: Width difference between backing paper and label in 0.1mm units (0-300, default: 10)
- **Sensor Type**: Method for detecting labels
  - None
  - Reflective
  - Transmissive (default)
  - Reflective Pre-Print
  - Transmissive Pre-Print

#### Label Processing

- **Cut Label**: Enable or disable label cutting if cutter is installed
  - Non-Cut (default)
  - Cut
- **Cut Interval**: Number of labels to print before cutting (0-100, 0=no cut, default: 0)
- **Feed Mode**: Label feeding behavior if peel off device is insalled
  - Batch Mode (default)
  - Strip Mode (Backfeed with Sensor)
  - Strip Mode (Backfeed without Sensor)
  - Partial Cut Mode
- **Feed on Label Size Change**: Whether to feed when label size changes (Yes/No, default: Yes). Should be enabled for most printers to not result in a command error.

#### Fine-Tuning Adjustments

- **Feed Adjustment**: Fine-tune label feed position in 0.1mm units (-500 to 500, negative=forward, positive=backward, default: 0)
- **Cut Position Adjustment**: Fine-tune cut position if cutter installed in 0.1mm units (-180 to 180, negative=forward, positive=backward, default: 0)
- **Backfeed Adjustment**: Fine-tune backfeed amount in 0.1mm units (-99 to 99, negative=decrease, positive=increase, default: 0)
- **Print Speed**: Printing speed in arbitrary units (varies by model). See [printer_properties.md](printer_properties.md) to convert to inches / second.
- **Print Darkness**: Darkness adjustment (-10 to 10, default: 0)

#### Graphics and Printing (only for expert use)

- **Data Transmission Mode**: How graphics data is sent to the printer
  - Nibble AND
  - Hex AND
  - TOPIX (default, recommended for best performance)
  - Nibble OR
  - Hex OR
- **Dithering Algorithm**: Algorithm for converting grayscale to black & white
  - Threshold (default)
  - Bayer
  - Clustered
- **Dithering Algorithm (Photo)**: Separate dithering algorithm for photo content
  - Threshold (default)
  - Bayer
  - Clustered
- **Dithering Threshold**: Threshold level when using threshold algorithm (0-255, default: 128)

### Printer Identification

The "Identify Printer" function in the web interface will feed a single blank label to help you identify which physical printer corresponds to the application.

### Test Page

The test page prints concentric boxes that are useful for verifying label positioning and making fine adjustments to margins and alignment.

## Sending Commands Directly to the Printer

You can send TPCL commands directly to the printer for testing or advanced operations. See the `/tests` directory for examples of direct command scripts.

## Build Instructions

### Build Tools and Dependencies

**For Linux (openSUSE/SUSE):**

```bash
sudo zypper install gcc gcc-c++ make pkg-config \
    cups-devel libppd libppd-devel cups-ddk \
    libjpeg8-devel libpng16-devel zlib-devel \
    libavahi-devel libopenssl-devel libusb-1_0-devel pam-devel \
    ImageMagick
```

**For Linux (Debian/Ubuntu):**

```bash
sudo apt-get install build-essential gcc make pkg-config \
    libcups2-dev libjpeg-dev libpng-dev zlib1g-dev \
    libavahi-client-dev libssl-dev libusb-1.0-0-dev libpam0g-dev \ 
    imagemagick
```

**For macOS:**

```bash
brew install cups libjpeg libpng zlib openssl libusb imagemagick
```

### Building

**Full build** (including PAPPL framework):

```bash
make full
```

**Quick build** (without rebuilding PAPPL):

```bash
make
```

**Further build options**

```bash
make help
```

### Build Notes

- Translations are patched directly into PAPPL during the build process. See `/scripts` for details.
- ImageMagick is used to convert icon images to C header files during the build.

## Technical Details

This is an IPP (Internet Printing Protocol) Printer Application built on the [PAPPL framework](https://github.com/michaelrsweet/pappl/). PAPPL is a modern printer application framework that replaces the older PPD-based system used by CUPS.

The printer application converts raster graphics into TPCL commands with support for TOPIX compression for reliable and fast delivery of print jobs. It provides full IPP support, allowing the printer to be accessed over the network using standard printing protocols.

### Viewing Logs

Run with debug logging enabled:

```bash
sudo ./bin/tpcl-printer-app server -o log-level=debug
```

Monitor the application logs in debug mode:

```bash
sudo tail -f /tmp/pappl$(pidof tpcl-printer-app).log
```

### Configuration Files

**System state file** (`tpcl-printer-app.state` - stores system and printer configuration):
* Root (Linux/BSD): `/var/lib/tpcl-printer-app.state`
* User (Linux/BSD): `$XDG_CONFIG_HOME/tpcl-printer-app.state` (if set) or `$HOME/.config/tpcl-printer-app.state`
* User (macOS): `$HOME/Library/Application Support/tpcl-printer-app.state`
* Snap: `$SNAP_COMMON/tpcl-printer-app.state`
* Fallback: `/tmp/tpcl-printer-app<uid>.state`

**Spool directory** (contains jobs and per-printer state file for label size tracking):
* Root (Linux/BSD): `/var/spool/tpcl-printer-app/`
* User (Linux/BSD): `$HOME/.config/tpcl-printer-app.d/`
* User (macOS): `$HOME/Library/Application Support/tpcl-printer-app.d/`
* Snap: `$SNAP_COMMON/tpcl-printer-app.d/`
* Fallback: `/tmp/tpcl-printer-app<uid>.d/`

**Configuration file** (`tpcl-printer-app.conf` - stores server options, loaded in order):
* User (Linux/BSD): `$XDG_CONFIG_HOME/tpcl-printer-app.conf` (if set) or `$HOME/.config/tpcl-printer-app.conf`
* User (macOS): `$HOME/Library/Application Support/tpcl-printer-app.conf`
* System (Linux/BSD): `/usr/local/etc/tpcl-printer-app.conf`, then `/etc/tpcl-printer-app.conf`
* System (macOS): `/Library/Application Support/tpcl-printer-app.conf`
* Snap: `$SNAP_COMMON/tpcl-printer-app.conf`

**Logs:**
* Default: system logging provider
* Debug mode: `/tmp/pappl<pid>.log`
* Nibble mode dump (debug only): `/tmp/rastertotpcl-nibble-dump-<pid>.out`

## Project History

This project began as the rastertotpcl PPD-based CUPS filter and has been migrated to a modern IPP Printer Application based on the PAPPL framework.

### Original PPD-Based Implementation

The original rastertotpcl driver converted CUPS Raster graphics along with a supported PPD file into TPCL graphics ready to be printed directly. It included support for the TPCL TOPIX compression algorithm for reliable and fast delivery of print jobs to the printer.

The original source can be found at [samlown/rastertotpcl](http://github.com/samlown/rastertotpcl).

## License

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

## Authors

- **rastertotpcl** is based on the **rastertotec** driver written by Patrick Kong (SKE s.a.r.l)
- **rastertotec** is based on the **rastertolabel** driver included with the CUPS printing system by Easy Software Products
- Packaging of rastertotpcl and TOPIX compression was added by Sam Lown (www.samlown.com)
- Original MacOS adaptation by [Milverton](https://milverton.typepad.com/the-hairy-mouse/2011/10/print-to-toshiba-tec-b-ev4d-gs14-on-os-x.html)
- PAPPL migration and current maintenance by Mark Dornbach (yaourdt)
