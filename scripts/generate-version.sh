#!/bin/bash
# Generate version string from git and update RPM spec file
#
# Version logic:
# - If GIT_COMMIT env var is set: use that value
# - If on a tagged commit: use the tag (e.g., v0.2.2 -> 0.2.2)
# - If not on tagged commit: use first 8 digits of commit hash (e.g., 07c193ce)
# - If files have changed: append '-dirty' suffix

OUTPUT_FILE="src/version.h"
SPEC_FILE="tpcl-printer-app.spec"

# Check if GIT_COMMIT environment variable is set
if [ -n "$GIT_COMMIT" ]; then
    VERSION="$GIT_COMMIT"
else
    # Try to get exact tag at current commit
    VERSION=$(git describe --tags --exact-match 2>/dev/null)

    if [ -z "$VERSION" ]; then
        # Not on a tagged commit, use commit hash (8 digits)
        VERSION=$(git rev-parse --short=8 HEAD 2>/dev/null || echo "unknown")
    fi

    # Check for uncommitted changes
    if ! git diff-index --quiet HEAD -- 2>/dev/null; then
        VERSION="${VERSION}-dirty"
    fi
fi

# Remove 'v' prefix if present (for consistency)
VERSION=$(echo "$VERSION" | sed 's/^v//')

# Generate header file
cat > "$OUTPUT_FILE" << EOF
/* Auto-generated version file - DO NOT EDIT */
#ifndef TPCL_VERSION_H
#define TPCL_VERSION_H

#define TPCL_VERSION "$VERSION"

#endif /* TPCL_VERSION_H */
EOF

echo "Generated version: $VERSION"

# Update RPM spec file version if it exists
if [ -f "$SPEC_FILE" ]; then
    # Update Version: line in spec file
    sed -i "s/^Version:.*/Version:        $VERSION/" "$SPEC_FILE"
    echo "Updated RPM spec version to: $VERSION"
fi
