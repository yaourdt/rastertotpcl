#!/bin/bash
# Uninstall script for TPCL Printer Application on macOS

set -e

# Check if running as root
if [ "$(id -u)" -ne 0 ]; then
    echo "Error: This script must be run as root"
    echo "Please run: sudo $0"
    exit 1
fi

echo "Uninstalling TPCL Printer Application..."
echo ""

# Stop and unload the LaunchDaemon
if launchctl list | grep -q com.yaourdt.tpcl-printer-app; then
    echo "Stopping service..."
    launchctl unload /Library/LaunchDaemons/com.yaourdt.tpcl-printer-app.plist 2>/dev/null || true
fi

# Remove the LaunchDaemon plist
if [ -f /Library/LaunchDaemons/com.yaourdt.tpcl-printer-app.plist ]; then
    echo "Removing LaunchDaemon..."
    rm -f /Library/LaunchDaemons/com.yaourdt.tpcl-printer-app.plist
fi

# Remove the binary
if [ -f /usr/local/bin/tpcl-printer-app ]; then
    echo "Removing binary..."
    rm -f /usr/local/bin/tpcl-printer-app
fi

echo ""
echo "TPCL Printer Application has been uninstalled."
echo ""
echo "The following data directories were preserved:"
echo "  - /var/lib/tpcl-printer-app.state (configuration)"
echo "  - /var/spool/tpcl-printer-app/ (print jobs)"
echo "  - /var/log/tpcl-printer-app.log (logs)"
echo ""
echo "To completely remove all data, run:"
echo "  sudo rm -f /var/lib/tpcl-printer-app.state"
echo "  sudo rm -rf /var/spool/tpcl-printer-app/"
echo "  sudo rm -f /var/log/tpcl-printer-app.log"
echo ""
