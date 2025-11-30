#!/bin/bash
# Generate version string from git and optionally update version files
#
# Usage:
#   generate-version.sh                    # Generate version.h only (local development)
#   generate-version.sh --print            # Print version to stdout and exit
#   generate-version.sh --update-packages  # Generate version.h AND update package files
#
# Version logic:
# - If GIT_COMMIT env var is set: use that value
# - If on a tagged commit: use the tag (e.g., v0.2.2 -> 0.2.2)
# - If not on tagged commit: use first 8 digits of commit hash (e.g., 07c193ce)
# - If files have changed: append '-dirty' suffix

set -e

# Parse command-line arguments
MODE="header-only"  # Default mode
while [[ $# -gt 0 ]]; do
    case $1 in
        --print)
            MODE="print"
            shift
            ;;
        --update-packages)
            MODE="update-packages"
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Generate version string from git state."
            echo ""
            echo "Options:"
            echo "  (no options)         Generate src/version.h only (local development)"
            echo "  --print              Print version to stdout and exit"
            echo "  --update-packages    Generate version.h AND update package files"
            echo "  --help, -h           Show this help message"
            echo ""
            echo "Environment Variables:"
            echo "  GIT_COMMIT           Override version detection (used in package builds)"
            exit 0
            ;;
        *)
            echo "Error: Unknown option $1" >&2
            echo "Run '$0 --help' for usage information" >&2
            exit 1
            ;;
    esac
done

OUTPUT_FILE="src/version.h"
VERSION_FILES="tpcl-printer-app.spec debian/changelog"

# Determine version from git
if [ -n "$GIT_COMMIT" ]; then
    VERSION="$GIT_COMMIT"
else
    # Try to get exact tag at current commit
    VERSION=$(git describe --tags --exact-match 2>/dev/null || true)

    if [ -z "$VERSION" ]; then
        # Not on a tagged commit, use commit hash (8 digits)
        VERSION=$(git rev-parse --short=8 HEAD 2>/dev/null || echo "unknown")
    fi

    # Check for uncommitted changes
    if ! git diff-index --quiet HEAD -- 2>/dev/null; then
        VERSION="${VERSION}.dirty"
    fi
fi

# Remove 'v' prefix if present (for consistency)
VERSION=$(echo "$VERSION" | sed 's/^v//')

# Print-only mode
if [ "$MODE" = "print" ]; then
    echo "$VERSION"
    exit 0
fi

# Generate header file
cat > "$OUTPUT_FILE" << EOF
/* Auto-generated version file - DO NOT EDIT */
#ifndef TPCL_VERSION_H
#define TPCL_VERSION_H

#define TPCL_VERSION "$VERSION"

#endif /* TPCL_VERSION_H */
EOF

echo "Generated version: $VERSION"

# Update package files if requested
if [ "$MODE" = "update-packages" ]; then
    echo "Updating package files..."

    # For Debian packaging: version must start with a digit
    # If version starts with a letter (git hash), prepend "0."
    PKG_VERSION="$VERSION"
    if ! echo "$PKG_VERSION" | grep -q '^[0-9]'; then
        PKG_VERSION="0.${PKG_VERSION}"
    fi

    # Update @@VERSION@@ placeholder in all version files
    for file in $VERSION_FILES; do
        if [ -f "$file" ]; then
            # Use portable sed syntax that works on both macOS and Linux
            if [[ "$OSTYPE" == "darwin"* ]]; then
                sed -i '' "s/@@VERSION@@/$PKG_VERSION/g" "$file"
            else
                sed -i "s/@@VERSION@@/$PKG_VERSION/g" "$file"
            fi
            echo "  Updated: $file (version: $PKG_VERSION)"
        fi
    done
fi
