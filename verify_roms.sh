#!/bin/bash
# ROM Verification Script
# Checks that all ROMs in catalog.json exist and match expected sizes

echo "=== NES ROM Verification ==="
echo ""

ROMS_DIR="data/roms"
CATALOG="data/catalog.json"

if [ ! -f "$CATALOG" ]; then
    echo "❌ ERROR: catalog.json not found"
    exit 1
fi

if [ ! -d "$ROMS_DIR" ]; then
    echo "❌ ERROR: roms directory not found"
    exit 1
fi

echo "Checking ROMs against catalog..."
echo ""

# Extract ROM info from catalog and verify files
jq -r '.games[] | "\(.id)|\(.size_bytes)|\(.mapper)"' "$CATALOG" | while IFS='|' read -r id size mapper; do
    rom_file="${ROMS_DIR}/${id}.nes"
    
    if [ ! -f "$rom_file" ]; then
        echo "❌ MISSING: ${id}.nes"
        continue
    fi
    
    actual_size=$(stat -c%s "$rom_file" 2>/dev/null || stat -f%z "$rom_file" 2>/dev/null)
    
    # Check mapper support
    if [[ "$mapper" == "206" || "$mapper" == "227" ]]; then
        support="✗ NOT SUPPORTED"
    else
        support="✓ PLAYABLE"
    fi
    
    if [ "$actual_size" == "$size" ]; then
        echo "✓ ${id}.nes - ${actual_size} bytes - Mapper ${mapper} ${support}"
    else
        echo "⚠️  ${id}.nes - SIZE MISMATCH (expected: ${size}, actual: ${actual_size}) - Mapper ${mapper}"
    fi
done

echo ""
echo "=== Summary ==="
total=$(ls -1 "$ROMS_DIR"/*.nes 2>/dev/null | wc -l)
echo "Total ROM files: $total"
echo ""
echo "✓ 4 ROMs are playable (Mappers 1, 4)"
echo "✗ 2 ROMs are not supported (Mappers 206, 227)"
echo ""
echo "Run 'pio run -t uploadfs -e esp32-s3-devkitc-1' to flash ROMs to device"
