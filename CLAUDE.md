# TPCL Printer App - Claude Code Context

## Project Overview

This project is migrating from a PPD-based raster filter for Toshiba Tec Label Printers (TPCL v2 protocol) to a new IPP implementation based on the PAPPL framework.

## Directory Structure

- `src_old/` - Legacy PPD-based filter implementation (reference only)
- `src/` - New PAPPL-based implementation (active development)

## Development Workflow

### Building and Running

Execute from within the `src/` directory:

```bash
make clean && make && sudo ./tpcl-printer-app server -o log-level=debug
```

### Viewing Logs

Monitor the application logs in real-time:

```bash
sudo tail -f /tmp/pappl$(pidof tpcl-printer-app).log
```

### Testing Print Jobs

**IMPORTANT**: Always confirm with the user before starting a print job.

Test print command example:

```bash
ipptool -tv -f /home/yaurdt/Downloads/tux.png ipp://localhost:8000/ipp/print/TestPrinter print-job.test
```

## Key Technologies

- **PAPPL Framework** - Printer Application framework
- **IPP** - Internet Printing Protocol
- **TPCL v2** - Toshiba Tec Printer Command Language version 2

## Current Status

Based on recent commits:
- Printer status polling implemented
- Status code reading functional
- Working on image rendering (test image printing in progress)
- Line callback implementation in development

## Git Information

- Current branch: `release/0.2.0`
- Main branch: `main` (use for PRs)

## Notes for Development

- The project is actively being developed in the `src/` directory
- Old implementation in `src_old/` should only be used as a reference
- Always run with sudo due to printer access requirements
- Log level can be adjusted via the `-o log-level=debug` option
