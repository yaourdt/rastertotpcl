#!/bin/bash
# Publish Debian repository to GitHub Pages
set -e

# Configuration
REPO_DIR="debian-repo"
DIST="stable"
COMPONENT="main"
ARCH="amd64"

if [ -z "$1" ]; then
    echo "Usage: $0 <path-to-deb-file>"
    exit 1
fi

DEB_FILE="$1"

if [ ! -f "$DEB_FILE" ]; then
    echo "Error: DEB file not found: $DEB_FILE"
    exit 1
fi

echo "Publishing Debian repository..."

# Extract package information
PACKAGE=$(dpkg-deb -f "$DEB_FILE" Package)
VERSION=$(dpkg-deb -f "$DEB_FILE" Version)
ARCHITECTURE=$(dpkg-deb -f "$DEB_FILE" Architecture)

echo "Package: $PACKAGE"
echo "Version: $VERSION"
echo "Architecture: $ARCHITECTURE"

# Create directory structure
mkdir -p "$REPO_DIR/pool/$COMPONENT/t/$PACKAGE"
mkdir -p "$REPO_DIR/dists/$DIST/$COMPONENT/binary-$ARCH"

# Copy DEB file to pool
DEB_FILENAME="${PACKAGE}_${VERSION}_${ARCHITECTURE}.deb"
cp "$DEB_FILE" "$REPO_DIR/pool/$COMPONENT/t/$PACKAGE/$DEB_FILENAME"

# Generate Packages file
cd "$REPO_DIR"
echo "Generating Packages file..."

# Create Packages file
PACKAGES_FILE="dists/$DIST/$COMPONENT/binary-$ARCH/Packages"
> "$PACKAGES_FILE"

# Add package information
dpkg-scanpackages --arch "$ARCH" "pool/$COMPONENT" /dev/null > "$PACKAGES_FILE"

# Compress Packages file
gzip -9 -k -f "$PACKAGES_FILE"

# Generate Release file
RELEASE_FILE="dists/$DIST/Release"
echo "Generating Release file..."

cat > "$RELEASE_FILE" << EOF
Origin: TPCL Printer App
Label: TPCL Printer App
Suite: $DIST
Codename: $DIST
Architectures: $ARCH
Components: $COMPONENT
Description: TPCL Printer Application Debian Repository
Date: $(date -Ru)
EOF

# Add file checksums to Release
echo "MD5Sum:" >> "$RELEASE_FILE"
for file in dists/$DIST/$COMPONENT/binary-$ARCH/Packages*; do
    if [ -f "$file" ]; then
        relative=$(echo "$file" | sed "s|dists/$DIST/||")
        md5=$(md5sum "$file" | cut -d' ' -f1)
        size=$(stat -c%s "$file" 2>/dev/null || stat -f%z "$file")
        printf " %s %16d %s\n" "$md5" "$size" "$relative" >> "$RELEASE_FILE"
    fi
done

echo "SHA1:" >> "$RELEASE_FILE"
for file in dists/$DIST/$COMPONENT/binary-$ARCH/Packages*; do
    if [ -f "$file" ]; then
        relative=$(echo "$file" | sed "s|dists/$DIST/||")
        sha1=$(sha1sum "$file" | cut -d' ' -f1)
        size=$(stat -c%s "$file" 2>/dev/null || stat -f%z "$file")
        printf " %s %16d %s\n" "$sha1" "$size" "$relative" >> "$RELEASE_FILE"
    fi
done

echo "SHA256:" >> "$RELEASE_FILE"
for file in dists/$DIST/$COMPONENT/binary-$ARCH/Packages*; do
    if [ -f "$file" ]; then
        relative=$(echo "$file" | sed "s|dists/$DIST/||")
        sha256=$(sha256sum "$file" | cut -d' ' -f1)
        size=$(stat -c%s "$file" 2>/dev/null || stat -f%z "$file")
        printf " %s %16d %s\n" "$sha256" "$size" "$relative" >> "$RELEASE_FILE"
    fi
done

# Sign Release file (if GPG key is available)
if command -v gpg &> /dev/null && [ -n "$GPG_KEY_ID" ]; then
    echo "Signing Release file..."

    # Create detached signature
    gpg --default-key "$GPG_KEY_ID" \
        --armor \
        --detach-sign \
        --output "dists/$DIST/Release.gpg" \
        "dists/$DIST/Release"

    # Create clearsigned InRelease
    gpg --default-key "$GPG_KEY_ID" \
        --armor \
        --clearsign \
        --output "dists/$DIST/InRelease" \
        "dists/$DIST/Release"

    echo "Repository signed successfully"
else
    echo "Warning: GPG not available or GPG_KEY_ID not set, repository will not be signed"
fi

cd ..

echo ""
echo "Debian repository published successfully!"
echo "Location: $REPO_DIR/"
echo ""
echo "Repository structure:"
tree "$REPO_DIR" 2>/dev/null || find "$REPO_DIR" -type f
echo ""
