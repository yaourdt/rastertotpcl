#!/bin/bash
#
# Script to patch PAPPL translation files with custom translations.
# Merges custom strings from strings/ directory into PAPPL's strings files
# and regenerates corresponding header files using standard Unix tools.
#

STRINGS_DIR="strings"
PAPPL_STRINGS_DIR="external/pappl/pappl/strings"

# Check if directories exist
if [ ! -d "$STRINGS_DIR" ]; then
    echo "Warning: Custom strings directory '$STRINGS_DIR' not found"
    exit 0
fi

if [ ! -d "$PAPPL_STRINGS_DIR" ]; then
    echo "Error: PAPPL strings directory '$PAPPL_STRINGS_DIR' not found"
    exit 1
fi

#
# Function to escape special characters for sed
#
escape_sed() {
    echo "$1" | sed -e 's/[\/&]/\\&/g' -e 's/\$/\\$/g'
}

#
# Function to merge strings files
# Args: $1 = custom file, $2 = target PAPPL file
#
merge_strings_file() {
    local custom_file="$1"
    local pappl_file="$2"
    local temp_file="${pappl_file}.tmp"

    # Create a copy to work with
    cp "$pappl_file" "$temp_file"

    # Read custom file line by line
    while IFS= read -r line; do
        # Skip empty lines and comments
        if [[ -z "$line" || "$line" =~ ^[[:space:]]*# ]]; then
            continue
        fi

        # Extract key from line: "key" = "value";
        if [[ "$line" =~ ^\"([^\"]+)\"[[:space:]]*=[[:space:]]*\"([^\"]*)\"\;?$ ]]; then
            key="${BASH_REMATCH[1]}"
            value="${BASH_REMATCH[2]}"

            # Escape special characters for grep
            escaped_key=$(echo "$key" | sed 's/[.[\*^$()+?{|]/\\&/g')

            # Check if key exists in target file
            if grep -q "^\"${escaped_key}\"" "$temp_file" 2>/dev/null; then
                # Key exists - update the value
                # Use awk for more reliable replacement
                awk -v key="$key" -v value="$value" '
                    BEGIN {found=0}
                    {
                        if ($0 ~ "^\"" key "\"") {
                            print "\"" key "\" = \"" value "\";";
                            found=1;
                        } else {
                            print $0;
                        }
                    }
                ' "$temp_file" > "${temp_file}.new"
                mv "${temp_file}.new" "$temp_file"
            else
                # Key doesn't exist - append new line
                echo "$line" >> "$temp_file"
            fi
        fi
    done < "$custom_file"

    # Replace original file
    mv "$temp_file" "$pappl_file"
}

#
# Function to generate header from strings file
# Args: $1 = strings file, $2 = header file, $3 = language code
#
generate_header() {
    local strings_file="$1"
    local header_file="$2"
    local lang_code="$3"

    # Sanitize language code for C variable name (replace - with _)
    local var_name=$(echo "$lang_code" | tr '-' '_')

    # Start header file
    echo "static const char *${var_name}_strings = " > "$header_file"

    # Process each line of the strings file
    local first_line=true
    while IFS= read -r line; do
        # Skip empty lines and comments
        if [[ -z "$line" || "$line" =~ ^[[:space:]]*# || "$line" =~ ^[[:space:]]*\/\* ]]; then
            continue
        fi

        # Escape the line for C string: backslashes and quotes
        # Then add \n at the end and wrap in quotes
        escaped_line=$(echo "$line" | sed 's/\\/\\\\/g' | sed 's/"/\\"/g')

        if [ "$first_line" = true ]; then
            echo "\"${escaped_line}\\n\"" >> "$header_file"
            first_line=false
        else
            echo "\"${escaped_line}\\n\"" >> "$header_file"
        fi
    done < "$strings_file"

    # Close the string
    echo ";" >> "$header_file"
}

echo "Patching PAPPL translations with custom strings..."

# Iterate through all .strings files in custom directory
for custom_file in "$STRINGS_DIR"/*.strings; do
    if [ -f "$custom_file" ]; then
        # Get the base name (e.g., "de.strings")
        base_name=$(basename "$custom_file")
        lang_code="${base_name%.strings}"
        pappl_file="$PAPPL_STRINGS_DIR/$base_name"
        header_file="$PAPPL_STRINGS_DIR/${lang_code}_strings.h"

        # Check if corresponding PAPPL file exists
        if [ -f "$pappl_file" ]; then
            echo "  $base_name: Merging translations..."

            # Merge strings
            merge_strings_file "$custom_file" "$pappl_file"

            # Regenerate header
            echo "  $base_name: Regenerating header ${lang_code}_strings.h..."
            generate_header "$pappl_file" "$header_file" "$lang_code"

            echo "  $base_name: Patched successfully"

            # If this is the English file, also patch base.strings
            if [ "$lang_code" = "en" ]; then
                echo "  Patching base.strings from en.strings..."
                base_strings_file="external/pappl/pappl/base.strings"

                # Merge en custom strings into base.strings
                if [ -f "$base_strings_file" ]; then
                    merge_strings_file "$custom_file" "$base_strings_file"
                    echo "  base.strings patched successfully"
                else
                    echo "  Warning: base.strings not found at $base_strings_file"
                fi
            fi
        else
            echo "  $base_name: No matching PAPPL file found, skipping..."
        fi
    fi
done

echo "Translation patching complete."
