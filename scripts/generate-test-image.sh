#!/bin/bash
#
# generate-test-image.sh - Generate test images for TPCL printer testing
#
# Creates a monochrome (1-bit black and white) PNG with concentric rectangular
# frames alternating between black and white, starting with black on the outside.
#
# Usage: generate-test-image.sh <dpi> <width_mm> <height_mm> [output_file]
#
# Arguments:
#   dpi         - Resolution in dots per inch (e.g., 203, 300)
#   width_mm    - Label width in millimeters
#   height_mm   - Label height in millimeters
#   output_file - Optional output filename (default: test_<width>x<height>_<dpi>dpi.png)
#

set -e

# Check for ImageMagick
if ! command -v convert &> /dev/null && ! command -v magick &> /dev/null; then
    echo "Error: ImageMagick is not installed" >&2
    echo "Install with: brew install imagemagick (macOS) or apt-get install imagemagick (Linux)" >&2
    exit 1
fi

# Use 'magick' if available (ImageMagick 7+), otherwise 'convert' (ImageMagick 6)
if command -v magick &> /dev/null; then
    MAGICK_CMD="magick"
else
    MAGICK_CMD="convert"
fi

# Check arguments
if [ "$#" -lt 3 ]; then
    echo "Usage: $0 <dpi> <width_mm> <height_mm> [output_file]" >&2
    echo "" >&2
    echo "Example: $0 203 100 50" >&2
    echo "  Generates a 203 DPI test image for a 100mm x 50mm label" >&2
    exit 1
fi

DPI=$1
WIDTH_MM=$2
HEIGHT_MM=$3
OUTPUT_FILE=${4:-"test_${WIDTH_MM}x${HEIGHT_MM}_${DPI}dpi.png"}

# Validate numeric inputs
if ! [[ "$DPI" =~ ^[0-9]+$ ]] || ! [[ "$WIDTH_MM" =~ ^[0-9]+(\.[0-9]+)?$ ]] || ! [[ "$HEIGHT_MM" =~ ^[0-9]+(\.[0-9]+)?$ ]]; then
    echo "Error: DPI, width, and height must be numeric values" >&2
    exit 1
fi

# Calculate pixel dimensions
# Formula: pixels = (mm / 25.4) * dpi
WIDTH_PX=$(awk "BEGIN {printf \"%.0f\", ($WIDTH_MM / 25.4) * $DPI}")
HEIGHT_PX=$(awk "BEGIN {printf \"%.0f\", ($HEIGHT_MM / 25.4) * $DPI}")

echo "Generating test image:"
echo "  Physical size: ${WIDTH_MM}mm x ${HEIGHT_MM}mm"
echo "  Resolution: ${DPI} DPI"
echo "  Pixel dimensions: ${WIDTH_PX}px x ${HEIGHT_PX}px"
echo "  Pattern: 10 concentric frames (5 black, 5 white)"
echo "  Output: $OUTPUT_FILE"

# Determine the smaller dimension for band width calculations
MIN_DIM=$WIDTH_PX
if [ "$HEIGHT_PX" -lt "$WIDTH_PX" ]; then
    MIN_DIM=$HEIGHT_PX
fi

# Calculate step size for 10 bands from edge to center
# Since we go from edge to center (50% of dimension), each of 10 bands is 5% of the smaller dimension
STEP=$(echo "scale=2; $MIN_DIM * 0.05" | bc)

# Build ImageMagick drawing commands for concentric rectangles
# Pattern: Black, White, Black, White, Black, White, Black, White, Black, White (center)
# We draw from outside to inside so each smaller rectangle sits on top, creating visible bands

DRAW_CMDS=""

# Draw 10 rectangles (from outside to inside)
for i in {0..9}; do
    # Calculate inset for this rectangle
    INSET=$(awk "BEGIN {printf \"%.0f\", $STEP * $i}")

    # Calculate coordinates
    X1=$INSET
    Y1=$INSET
    X2=$((WIDTH_PX - INSET - 1))
    Y2=$((HEIGHT_PX - INSET - 1))

    # Alternate between black (0,2,4,6,8) and white (1,3,5,7,9)
    if [ $((i % 2)) -eq 0 ]; then
        COLOR="black"
    else
        COLOR="white"
    fi

    # Add drawing command
    DRAW_CMDS="$DRAW_CMDS -fill $COLOR -draw 'rectangle $X1,$Y1 $X2,$Y2'"
done

# Calculate innermost rectangle bounds for orientation triangle
# The innermost rectangle is at i=9, so inset is STEP * 9
INNER_INSET=$(awk "BEGIN {printf \"%.0f\", $STEP * 9}")
INNER_X1=$INNER_INSET
INNER_Y1=$INNER_INSET
INNER_X2=$((WIDTH_PX - INNER_INSET - 1))
INNER_Y2=$((HEIGHT_PX - INNER_INSET - 1))

# Calculate triangle dimensions (use 60% of innermost box width, centered)
INNER_WIDTH=$((INNER_X2 - INNER_X1))
INNER_HEIGHT=$((INNER_Y2 - INNER_Y1))
TRI_WIDTH=$(awk "BEGIN {printf \"%.0f\", $INNER_WIDTH * 0.6}")
TRI_HEIGHT=$(awk "BEGIN {printf \"%.0f\", $INNER_HEIGHT * 0.6}")

# Triangle coordinates (pointing upward)
TRI_TOP_X=$((INNER_X1 + INNER_WIDTH / 2))
TRI_TOP_Y=$((INNER_Y1 + (INNER_HEIGHT - TRI_HEIGHT) / 2))
TRI_BOTTOM_LEFT_X=$((TRI_TOP_X - TRI_WIDTH / 2))
TRI_BOTTOM_LEFT_Y=$((TRI_TOP_Y + TRI_HEIGHT))
TRI_BOTTOM_RIGHT_X=$((TRI_TOP_X + TRI_WIDTH / 2))
TRI_BOTTOM_RIGHT_Y=$TRI_BOTTOM_LEFT_Y

# Generate the image
# Start with a black canvas (since outermost is black), then draw all rectangles, then add orientation triangle
# Set density to match the requested DPI so PAPPL/CUPS knows the physical size
eval "$MAGICK_CMD -size ${WIDTH_PX}x${HEIGHT_PX} -density ${DPI} xc:black $DRAW_CMDS -fill black -draw 'polygon $TRI_TOP_X,$TRI_TOP_Y $TRI_BOTTOM_LEFT_X,$TRI_BOTTOM_LEFT_Y $TRI_BOTTOM_RIGHT_X,$TRI_BOTTOM_RIGHT_Y' -type bilevel -units PixelsPerInch \"$OUTPUT_FILE\""

echo "✓ Test image generated successfully: $OUTPUT_FILE"

# Display image info
file "$OUTPUT_FILE" 2>/dev/null || true
