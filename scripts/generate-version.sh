#!/bin/bash
# Generate version string from git and update version files
#
# Version logic:
# - If GIT_COMMIT env var is set: use that value
# - If on a tagged commit: use the tag (e.g., v0.2.2 -> 0.2.2)
# - If not on tagged commit: use first 8 digits of commit hash (e.g., 07c193ce)
# - If files have changed: append '-dirty' suffix
#
# Updates @@VERSION@@ placeholder in:
# - tpcl-printer-app.spec
# - debian/changelog

OUTPUT_FILE="src/version.h"
VERSION_FILES="tpcl-printer-app.spec debian/changelog"

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
        VERSION="${VERSION}.dirty"
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

# For Debian packaging: version must start with a digit
# If version starts with a letter (git hash), prepend "0."
if ! echo "$VERSION" | grep -q '^[0-9]'; then
    VERSION="0.${VERSION}"
fi

# Update @@VERSION@@ placeholder in all version files
for file in $VERSION_FILES; do
    if [ -f "$file" ]; then
        # Use portable sed syntax that works on both macOS and Linux
        if [[ "$OSTYPE" == "darwin"* ]]; then
            sed -i '' "s/@@VERSION@@/$VERSION/g" "$file"
        else
            sed -i "s/@@VERSION@@/$VERSION/g" "$file"
        fi
        echo "Updated version in: $file (version: $VERSION)"
    fi
done
