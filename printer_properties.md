# TPCL v2 Driver Configuration Reference
Auto-generated from the original 'tectpcl2.drv' file.

## General Settings Overview

| Setting Category | Option | Default Value | Available Values |
|-----------------|---------|---------------|------------------|
| **General** | Gap (Label Gap) | 2 mm | 0-5 mm (integer steps) |
| **General** | teMediaTracking (Media Detection) | Transmissive | Continuous, Reflective, Transmissive, Reflective Pre-Print, Transmissive Pre-Print |
| **General** | PrintOrient (Orientation) | Bottom Leading | Bottom Leading, Top Leading, Mirror Print Top Leading, Mirror Print Bottom Leading |
| **General** | CenterOfPixel | true | true, false |
| **Printer Settings** | Darkness (Temperature) | 0 (value 11) | -10 to +10 (values 1-21) |
| **Printer Settings** | tePrintMode (Print Mode) | Batch | Batch, Peel Off Cell Active, Peel Off Cell Not Active, Cutter |
| **Printer Settings** | teCutter (Cutter Option) | No Cut | No Cut, Cut Every Label, Cut Every 2 Labels |
| **Advanced Settings** | teGraphicsMode (Graphics Mode) | TOPIX Compression | TOPIX Compression, Raw 8bit Graphics (overwrite), Raw 8bit Graphics (logic OR) |
| **Advanced Settings** | FAdjSgn (Feed Direction) | + | +, - |
| **Advanced Settings** | FAdjV (Feed Adjust Value) | 0.0 mm | 0.0-10.0 mm (1.0 mm steps) |
| **Advanced Settings** | CAdjSgn (Cut/Peel Adjust Direction) | + | +, - |
| **Advanced Settings** | CAdjV (Cut/Peel Adjust Value) | 0.0 mm | 0.0-10.0 mm (1.0 mm steps) |
| **Advanced Settings** | RAdjSgn (Back Feed Direction) | + | +, - |
| **Advanced Settings** | RAdjV (Back Feed Adjust Value) | 0.0 mm | 0.0-9.9 mm (1.0 mm steps, max 9.9) |
| **Advanced Settings** | RbnAdjFwd (Ribbon Forward Adjust) | 0 | 0 to -15 |
| **Advanced Settings** | RbnAdjBck (Ribbon Back Adjust) | 0 | 0 to -15 |

### Global Attributes

| Attribute | Value |
|-----------|-------|
| DriverType | label |
| ModelNumber | 0x00 |
| ColorDevice | No |
| Throughput | 8 |
| Manufacturer | Toshiba Tec |

### Installable Options

| Option | Name |
|--------|------|
| Cutter | Cutter |
| Peel | Peel Off |

### UI Constraints

- Cutter False conflicts with teCutter 1, teCutter 2, tePrintMode 3
- Peel False conflicts with tePrintMode 1, tePrintMode 2
- tePrintMode 1 conflicts with teCutter 1

## Printer Models Configuration

| Model Name | VariablePaperSize | MinSize (pts) | MaxSize (pts) | Resolution(s) | Default Resolution | HWMargins |
|------------|-------------------|---------------|---------------|---------------|--------------------|-----------|
| B-SA4G | Yes | 63 x 29 | 300 x 2830 | 203dpi | 203dpi | 0 0 0 0 |
| B-SA4T | Yes | 63 x 29 | 300 x 2830 | 300dpi | 300dpi | 0 0 0 0 |
| B-SX4 | Yes | 72 x 23 | 295 x 4246 | 203dpi | 203dpi | 0 0 0 0 |
| B-SX5 | Yes | 73 x 29 | 362 x 4246 | 203dpi, 300dpi | 300dpi | 0 0 0 0 |
| B-SX6 | Yes | 238 x 29 | 483 x 4246 | 203dpi, 300dpi | 300dpi | 0 0 0 0 |
| B-SX8 | Yes | 286 x 29 | 605 x 4246 | 203dpi, 300dpi | 300dpi | 0 0 0 0 |
| B-482 | Yes | 72 x 23 | 295 x 4246 | 203dpi, 300dpi | 300dpi | 0 0 0 0 |
| B-572 | Yes | 73 x 29 | 362 x 4246 | 203dpi, 300dpi | 300dpi | 0 0 0 0 |
| B-852R | Yes | 283 x 35 | 614 x 1814 | 300dpi | 300dpi | 0 0 0 0 |
| B-SV4D | Yes | 71 x 23 | 306 x 1726 | 203dpi | 203dpi | 0 0 0 0 |
| B-SV4T | Yes | 71 x 23 | 306 x 1726 | 203dpi | 203dpi | 0 0 0 0 |
| B-EV4D-G | Yes | 71 x 23 | 306 x 1726 | 203dpi | 203dpi | 0 0 0 0 |
| B-EV4D-T | Yes | 71 x 23 | 306 x 1726 | 300dpi | 300dpi | 0 0 0 0 |
| B-EV4T-G | Yes | 71 x 23 | 306 x 1726 | 203dpi | 203dpi | 0 0 0 0 |
| B-EV4T-T | Yes | 71 x 23 | 306 x 1726 | 300dpi | 300dpi | 0 0 0 0 |

## Printer-Specific Settings

### Media Type Support

| Model Name | Media Type Options | Default |
|------------|-------------------|---------|
| B-SA4G, B-SA4T | Direct Thermal, Thermal Transfer | Direct Thermal |
| B-SX4, B-SX5, B-SX6, B-SX8, B-482, B-572 | Direct Thermal, Thermal Transfer with ribbon saving, Thermal Transfer without ribbon saving | Direct Thermal |
| B-852R | (Not specified - inherits general settings) | N/A |
| B-SV4D | Direct Thermal only | Direct Thermal |
| B-SV4T | Thermal Transfer, Direct Thermal | Direct Thermal |
| B-EV4D-G, B-EV4D-T | Direct Thermal only | Direct Thermal |
| B-EV4T-G, B-EV4T-T | Thermal Transfer, Direct Thermal | Direct Thermal |

### Print Speed Settings (tePrintRate)

| Model Name | Available Speeds | Default Speed |
|------------|------------------|---------------|
| B-SA4G, B-SA4T | 50 mm/sec (2), 101 mm/sec (4), 152 mm/sec (6) | 101 mm/sec (4) |
| B-SX4 | 76 mm/sec (3), 152 mm/sec (6), 254 mm/sec (10) | 152 mm/sec (6) |
| B-SX5 | 76 mm/sec (3), 127 mm/sec (5), 203 mm/sec (8) | 127 mm/sec (5) |
| B-SX6 | 76 mm/sec (3), 101 mm/sec (4), 203 mm/sec (8) | 101 mm/sec (4) |
| B-SX8 | 76 mm/sec (3), 101 mm/sec (4), 203 mm/sec (8) | 101 mm/sec (4) |
| B-482 | 76 mm/sec (3), 102 mm/sec (4), 127 mm/sec (5), 203 mm/sec (8) | 127 mm/sec (5) |
| B-572 | 76 mm/sec (3), 127 mm/sec (5), 203 mm/sec (8) | 127 mm/sec (5) |
| B-852R | 50.8 mm/sec (2), 101 mm/sec (4), 203 mm/sec (8) | 101 mm/sec (4) |
| B-SV4D, B-SV4T, B-EV4D-G, B-EV4D-T, B-EV4T-G, B-EV4T-T | 50 mm/sec (2), 76.2 mm/sec (3), 101.6 mm/sec (4), 127.0 mm/sec (5) | 76.2 mm/sec (3) |

## Notes

1. **Point Sizes**: MinSize and MaxSize are specified in points (1 point = 1/72 inch)
2. **Resolution Naming**: Some resolutions have specific model suffixes (e.g., TECBSA4G, TECBEV4T) for identification
3. **Media Sizes**: All printers inherit the common media size definitions (w90h18 through w292h564), with some larger models (B-SX6, B-SX8) having additional wider formats
4. **HWMargins**: All printers use 0 0 0 0 (borderless printing)
5. **Printer Families**:
   - B-SA series: Entry-level models with basic features
   - B-SX series: Mid-range models with more speed options and media type choices
   - B-4xx/5xx series: Industrial models with extended features
   - B-SV series: Specialized models (desktop or mobile)
   - B-EV series: Latest generation with dual-resolution support
