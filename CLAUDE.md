# TPCL Printer App - Claude Code Context

## Project Overview

This project is migrating from a PPD-based raster filter for Toshiba Tec Label Printers (TPCL v2 protocol) to a new IPP implementation based on the PAPPL framework.

## Directory Structure

- `src_old/` - Legacy PPD-based filter implementation (reference only)
- `src/` - New PAPPL-based implementation (active development)

## Build System Architecture

The build system has **clean separation** between local development and package builds:

### Local Development Builds (Makefile)
- **Purpose**: For LOCAL development only
- **PAPPL source**: Git submodule
- **Git operations**: Required and fail loudly if errors occur
- **Version script**: Generates only `src/version.h` (default mode)
- **Package files**: Never modified (tpcl-printer-app.spec, debian/changelog keep @@VERSION@@ placeholders)

### Package Builds (debian/rules, .spec)
- **Purpose**: For DEB/RPM package creation in CI/CD
- **PAPPL source**: Downloaded release tarball
- **Git operations**: None (independent build logic)
- **Version script**: Called with `--update-packages` to update spec/changelog
- **Independence**: Never calls main Makefile, builds directly with `make -C src`

This separation ensures:
- No git dependency in package builds
- No accidental package file modifications during development
- Clear error messages (git failures are loud in dev, silent in packages)

## Development Workflow

### Building and Running

**Standard build:**
```bash
make
```

**Full build** (clean rebuild including PAPPL):
```bash
make full
```

**macOS build** (use SINGLE_ARCH=1 to avoid multi-arch link errors):
```bash
make SINGLE_ARCH=1
make full SINGLE_ARCH=1
```

**Run the application:**
```bash
sudo ./bin/tpcl-printer-app server -o log-level=debug
```

**Clean build artifacts:**
```bash
make clean
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

### Manual Installation

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

### RPM Package

An RPM package definition is available in `tpcl-printer-app.spec`. The package includes a systemd service for automatic startup at boot.

**Key differences from manual installation:**
- Binary installs to `/usr/bin/tpcl-printer-app` (not `/usr/local/bin/`)
- Includes systemd service at `/usr/lib/systemd/system/tpcl-printer-app.service`
- Service must be manually enabled: `sudo systemctl enable tpcl-printer-app`
- Configuration in `/var/lib/` and `/var/spool/` is preserved on uninstallation

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

## Version Management

The application version is automatically generated during build from git:

- **On tagged commit**: Uses the tag (e.g., `v0.2.2` → `0.2.2`)
- **Not on tagged commit**: Uses 8-character commit hash (e.g., `07c193ce`)
- **With uncommitted changes**: Appends `-dirty` suffix (e.g., `07c193ce-dirty`)

### Version Script Modes

The `scripts/generate-version.sh` script has three modes:

1. **Default (no options)** - Local development mode:
   ```bash
   ./scripts/generate-version.sh
   ```
   - Generates only `src/version.h`
   - Does NOT modify package files (tpcl-printer-app.spec, debian/changelog)
   - Used by Makefile during local builds

2. **Print mode** - Get current version:
   ```bash
   ./scripts/generate-version.sh --print
   ```
   - Prints version string to stdout and exits
   - Useful for scripts and checking current version
   - No files are created or modified

3. **Update packages mode** - Package build mode:
   ```bash
   ./scripts/generate-version.sh --update-packages
   ```
   - Generates `src/version.h` AND updates package files
   - Replaces `@@VERSION@@` placeholders in .spec and debian/changelog
   - Used by debian/rules and .spec during package builds
   - Should NOT be used during local development

### Checking Version

- **From built binary**: `./bin/tpcl-printer-app --version`
- **From git state**: `./scripts/generate-version.sh --print`
- **From version header** (after build): `cat src/version.h`

**DO NOT** manually edit version numbers in the code. The version is automatically determined from git state.

## Git Information

- Current branch: `release/0.2.3`
- Main branch: `main` (use for PRs)

**Development Workflow:**
- Development is always done on a `release/<version>` branch (e.g., `release/0.2.3`)
- When ready, the release branch is merged to `main` and tagged
- Never develop directly on `main`

## Testing GitHub Actions Locally with Act

The GitHub Actions release workflow can be tested locally using [act](https://github.com/nektos/act).

### Prerequisites

Install act on your system. On macOS:
```bash
brew install act
```

### Important: Commit Changes Before Testing

**CRITICAL:** You must commit any changes to `tpcl-printer-app.spec` or other files before testing with act. The `make clean` command resets the spec file using `git checkout`, which will remove any uncommitted changes.

### Before Every Test Run

1. **IMPORTANT: Clean the repository first:**
   ```bash
   make clean SINGLE_ARCH=1
   ```
   This ensures act tests start from a clean state (especially important since we made build system changes).

2. **Check the current version:**
   ```bash
   ./scripts/generate-version.sh --print
   ```
   This will show you the current version that will be used (e.g., `0.2.3.dirty`).

3. **Create/update the tag event file** at `/tmp/tag-push-event.json` with the current version:
   ```json
   {
     "ref": "refs/tags/v0.2.3.dirty",
     "ref_name": "v0.2.3.dirty",
     "ref_type": "tag",
     "repository": {
       "name": "rastertotpcl",
       "full_name": "yaourdt/rastertotpcl"
     }
   }
   ```
   Replace `0.2.3.dirty` with your actual version from step 2.

4. **If the version changed** (because you made new commits), update the event file to match.

### Running the Tests

Test the build job:
```bash
act push -P ubuntu-latest=catthehacker/ubuntu:act-latest \
  --artifact-server-path=/tmp/act-artifacts \
  -e /tmp/tag-push-event.json \
  -j build
```

This will:
- Build the application
- Create a binary tarball
- Build the RPM package
- Upload artifacts to `/tmp/act-artifacts/`

### Checking the Results

After a successful run, check the generated artifacts:
```bash
ls -lhR /tmp/act-artifacts/
```

Extract and inspect the RPM:
```bash
unzip -l /tmp/act-artifacts/1/rpm-package/rpm-package.zip
```

### Common Issues

- **Version mismatch errors**: The version in the event file doesn't match the current git state. Run `./scripts/generate-version.sh` and update the event file.
- **Spec file reset**: Uncommitted changes to `tpcl-printer-app.spec` were lost. Commit your changes before running act.
- **Long build times**: The first run downloads Docker images and builds PAPPL from scratch (~60 seconds). Subsequent runs are faster if the container is cached.

## Notes for Development

- The project is actively being developed in the `src/` directory
- Old implementation in `src_old/` should only be used as a reference
- Always run with sudo due to printer access requirements
- Log level can be adjusted via the `-o log-level=debug` option
- Supported platforms: Linux and macOS (Windows is not supported)
