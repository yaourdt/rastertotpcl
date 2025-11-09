# TPCL Printer App - Claude Code Context

## Project Overview

This project is migrating from a PPD-based raster filter for Toshiba Tec Label Printers (TPCL v2 protocol) to a new IPP implementation based on the PAPPL framework.

## Directory Structure

- `src_old/` - Legacy PPD-based filter implementation (reference only)
- `src/` - New PAPPL-based implementation (active development)

## Development Workflow

### Building and Running

Full build including PAPPL (ususally not necessary)
```bash
make full
```

Build without rebuilding PAPPL
```bash
make
```

Run
```bash
sudo ./bin/tpcl-printer-app server -o log-level=debug
```

### Viewing Logs

Monitor the application logs in real-time:

```bash
sudo tail -f /tmp/pappl$(pidof tpcl-printer-app).log
```

### Testing Print Jobs

The user will issue test jobs.

## Key Technologies

- **PAPPL Framework** - Printer Application framework
- **IPP** - Internet Printing Protocol
- **TPCL v2** - Toshiba Tec Printer Command Language version 2

## Installation

Install requires root privileges:
```bash
sudo make install
```

This installs the binary to `/usr/local/bin/tpcl-printer-app`.

Uninstall:
```bash
sudo make uninstall
```

**Note:** Data directories are NOT created during installation. PAPPL creates them automatically at runtime.

## Runtime File Locations

When running as root (typical deployment):

**Binary:**
- `/usr/local/bin/tpcl-printer-app`

**System state file** (stores system and printer configuration):
- `/var/lib/tpcl-printer-app.state`

**Spool directory** (contains jobs and per-printer state files):
- `/var/spool/tpcl-printer-app/`
- Per-printer state files: `p<printer-id>-labelstate.state` (e.g., `p00016-labelstate.state`)
- These files track label dimensions to detect size changes

**Configuration file** (server options, loaded in order):
- User: `$HOME/Library/Application Support/tpcl-printer-app.conf` (macOS) or `$HOME/.config/tpcl-printer-app.conf` (Linux)
- System: `/Library/Application Support/tpcl-printer-app.conf` (macOS) or `/usr/local/etc/tpcl-printer-app.conf`, then `/etc/tpcl-printer-app.conf` (Linux)

**Logs:**
- Default: system logging provider
- Debug mode: `/tmp/pappl<pid>.log`
- Nibble mode dump (debug only): `/tmp/rastertotpcl-nibble-dump-<pid>.out`

## State Management Implementation

The project uses `papplPrinterOpenFile()` for per-printer state management:
- Files are stored in PAPPL's spool directory
- One state file per printer
- Files are read/written during print jobs when label size changes
- Files are automatically deleted when printer is deleted
- PAPPL handles all directory creation and platform-specific paths

## Git Information

- Current branch: `release/0.2.3`
- Main branch: `main` (use for PRs)

## Notes for Development

- The project is actively being developed in the `src/` directory
- Old implementation in `src_old/` should only be used as a reference
- Always run with sudo due to printer access requirements
- Log level can be adjusted via the `-o log-level=debug` option
- Supported platforms: Linux and macOS (Windows is not supported)
