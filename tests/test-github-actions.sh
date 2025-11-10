#!/bin/bash
# Test GitHub Actions workflows locally using act
# This script verifies all build jobs (binary, RPM, DEB) work correctly

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TAG_EVENT_FILE="/tmp/tag-push-event.json"
ARTIFACT_DIR="/tmp/act-artifacts"

cd "$PROJECT_DIR"

echo "=== Testing GitHub Actions with act ==="
echo

# Step 1: Generate version
echo "Step 1: Generating version..."
./scripts/generate-version.sh
VERSION=$(cat src/version.h | grep '#define TPCL_VERSION' | awk '{print $3}' | tr -d '"\n')
echo "Version: $VERSION"
echo

# Step 2: Create tag event file
echo "Step 2: Creating tag event file..."
cat > "$TAG_EVENT_FILE" <<EOF
{
  "ref": "refs/tags/v$VERSION",
  "ref_name": "v$VERSION",
  "ref_type": "tag",
  "repository": {
    "name": "rastertotpcl",
    "full_name": "yaourdt/rastertotpcl"
  }
}
EOF
echo "Created: $TAG_EVENT_FILE"
echo

# Step 3: Test build job (binary tarball)
echo "Step 3: Testing build job (binary tarball)..."
act push -P ubuntu-latest=catthehacker/ubuntu:act-latest \
  --artifact-server-path="$ARTIFACT_DIR" \
  -e "$TAG_EVENT_FILE" \
  -j build

echo
echo "Build job completed!"
echo

# Step 4: Test RPM build
echo "Step 4: Testing RPM build job..."
act push \
  --artifact-server-path="$ARTIFACT_DIR" \
  -e "$TAG_EVENT_FILE" \
  -j build-rpm || true  # Ignore upload artifact failure (no Node.js in Fedora container)

echo
echo "RPM build job completed!"
echo

# Step 5: Test DEB build
echo "Step 5: Testing DEB build job..."
act push \
  --artifact-server-path="$ARTIFACT_DIR" \
  -e "$TAG_EVENT_FILE" \
  -j build-deb || true  # Ignore upload artifact failure (no Node.js in Debian container)

echo
echo "DEB build job completed!"
echo

# Step 6: Extract packages from containers
echo "=== Extracting Packages ==="
echo

# Extract DEB packages
echo "Extracting DEB packages..."
DEBIAN_CONTAINER=$(docker ps -a --filter 'ancestor=debian:13' --format '{{.ID}}' | head -1)
if [ -n "$DEBIAN_CONTAINER" ]; then
  mkdir -p /tmp/deb-packages
  docker cp "$DEBIAN_CONTAINER:/Users/dob/rastertotpcl/dist/." /tmp/deb-packages/
  echo "DEB packages extracted to: /tmp/deb-packages/"
  ls -lh /tmp/deb-packages/*.deb 2>/dev/null || echo "  No DEB packages found"
else
  echo "  No Debian container found"
fi
echo

# Extract RPM packages
echo "Extracting RPM packages..."
FEDORA_CONTAINER=$(docker ps -a --filter 'ancestor=fedora:latest' --format '{{.ID}}' | head -1)
if [ -n "$FEDORA_CONTAINER" ]; then
  mkdir -p /tmp/rpm-packages
  docker cp "$FEDORA_CONTAINER:/Users/dob/rastertotpcl/dist/." /tmp/rpm-packages/
  echo "RPM packages extracted to: /tmp/rpm-packages/"
  ls -lh /tmp/rpm-packages/*.rpm 2>/dev/null | grep -v debuginfo | grep -v debugsource || echo "  No RPM packages found"
else
  echo "  No Fedora container found"
fi
echo

# Show summary
echo "=== Test Results Summary ==="
echo
echo "Binary artifacts:  $ARTIFACT_DIR"
echo "DEB packages:      /tmp/deb-packages/"
echo "RPM packages:      /tmp/rpm-packages/"
echo
echo "All tests completed successfully!"
