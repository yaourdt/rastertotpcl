#!/bin/bash
# Build macOS .pkg installer for TPCL Printer Application
set -e

# Check if running on macOS
if [[ "$OSTYPE" != "darwin"* ]]; then
    echo "Error: This script must run on macOS"
    exit 1
fi

# Check if binary exists
if [ ! -f "bin/tpcl-printer-app" ]; then
    echo "Error: Binary not found at bin/tpcl-printer-app"
    echo "Please build the application first with 'make full'"
    exit 1
fi

# Get version
VERSION=$(./scripts/generate-version.sh --print)
echo "Building macOS package for version: $VERSION"

# Create temporary directory structure
PKG_ROOT=$(mktemp -d)
SCRIPTS_DIR=$(mktemp -d)

echo "Preparing package contents..."

# Create directory structure
mkdir -p "$PKG_ROOT/usr/local/bin"
mkdir -p "$PKG_ROOT/Library/LaunchDaemons"

# Copy binary
cp bin/tpcl-printer-app "$PKG_ROOT/usr/local/bin/"
chmod 755 "$PKG_ROOT/usr/local/bin/tpcl-printer-app"

# Copy LaunchDaemon plist
cp com.yaourdt.tpcl-printer-app.plist "$PKG_ROOT/Library/LaunchDaemons/"
chmod 644 "$PKG_ROOT/Library/LaunchDaemons/com.yaourdt.tpcl-printer-app.plist"

# Create postinstall script
cat > "$SCRIPTS_DIR/postinstall" << 'EOF'
#!/bin/bash
# Post-installation script for TPCL Printer Application

# Create necessary directories
mkdir -p /var/lib
mkdir -p /var/spool
mkdir -p /var/log

# Set up log file
touch /var/log/tpcl-printer-app.log
chmod 644 /var/log/tpcl-printer-app.log

# Load the LaunchDaemon
launchctl load /Library/LaunchDaemons/com.yaourdt.tpcl-printer-app.plist 2>/dev/null || true

echo "TPCL Printer Application installed successfully"
echo "The service has been started and will run at boot"
echo ""
echo "To stop the service:    sudo launchctl unload /Library/LaunchDaemons/com.yaourdt.tpcl-printer-app.plist"
echo "To start the service:   sudo launchctl load /Library/LaunchDaemons/com.yaourdt.tpcl-printer-app.plist"
echo "To view logs:           tail -f /var/log/tpcl-printer-app.log"
echo "Web interface:          http://localhost:8000"

exit 0
EOF

chmod 755 "$SCRIPTS_DIR/postinstall"

# Create preuninstall script (for future use)
cat > "$SCRIPTS_DIR/preinstall" << 'EOF'
#!/bin/bash
# Pre-installation script for TPCL Printer Application

# Unload the LaunchDaemon if it's already loaded
if launchctl list | grep -q com.yaourdt.tpcl-printer-app; then
    launchctl unload /Library/LaunchDaemons/com.yaourdt.tpcl-printer-app.plist 2>/dev/null || true
fi

exit 0
EOF

chmod 755 "$SCRIPTS_DIR/preinstall"

# Create output directory
mkdir -p dist

# Build the package
PKG_NAME="tpcl-printer-app-${VERSION}.pkg"
echo "Building package: $PKG_NAME"

pkgbuild \
    --root "$PKG_ROOT" \
    --scripts "$SCRIPTS_DIR" \
    --identifier "com.yaourdt.tpcl-printer-app" \
    --version "$VERSION" \
    --install-location "/" \
    "dist/$PKG_NAME"

# Clean up
rm -rf "$PKG_ROOT"
rm -rf "$SCRIPTS_DIR"

echo ""
echo "Package created successfully: dist/$PKG_NAME"
echo ""
echo "To install: sudo installer -pkg dist/$PKG_NAME -target /"
echo ""
