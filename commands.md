# TPCL Commands Reference

This document lists all TPCL commands sent to the printer during the printing process, extracted from the old `rastertotpcl.c` implementation.

## Start Job Stage

### Setup() Function - Lines 101-191

1. `{WR|}` - Reset command to clear printer state
2. `{AX;±XX,±XX,±XX|}` - Feed adjustment command (feed, cut/peel, back feed adjustments)
   - First parameter: Feed adjust (sign + value)
   - Second parameter: Cut/peel adjust (sign + value)
   - Third parameter: Back feed adjust (sign + value)
3. `{RM;X,X|}` - Ribbon motor setup
   - First parameter: Forward motor setting
   - Second parameter: Backward motor setting

## Start Page Stage

### StartPage() Function - Lines 197-422

4. `{DXXXX,XXXX,XXXX,XXXX|}` - Label size definition (line 295)
   - First parameter: Label pitch (label length + gap) in 0.1mm units
   - Second parameter: Label width in 0.1mm units
   - Third parameter: Label length in 0.1mm units
   - Fourth parameter: Peel-off position (width + gap) in 0.1mm units

5. `{AY;±XX,X|}` - Temperature fine adjustment (line 379)
   - First parameter: Temperature adjustment (-10 to +10)
   - Second parameter: Print mode (0=thermal transfer, 1=direct thermal)

6. `{C|}` - Clear image buffer (line 382)

7. **IF NOT TOPIX MODE:** `{SG;0000,0000,XXXX,XXXX,X,` - Start graphics header for HEX mode (line 401)
   - Position X: 0000
   - Position Y: 0000
   - Width: cupsBytesPerLine * 8 (in dots)
   - Height: cupsHeight (in dots)
   - Mode: Graphics mode (1=HEX AND, 5=HEX OR)
   - Followed by raw hex data, terminated by `|}\n`

## Write Line Stage

### OutputLine() Function - Lines 660-673

**IF TOPIX MODE:**
- Lines are compressed and buffered via `TOPIXCompress()`
- No direct output per line

**IF HEX MODE:**
- Raw binary data written directly to stdout (line 670)

### TOPIXCompress() Function - Lines 680-764

**TOPIX Buffer Management:**
- When buffer gets near full (> 0xFFFF - (width + (width/8)*3)), flush buffer via `TOPIXCompressOutputBuffer()`

### TOPIXCompressOutputBuffer() Function - Lines 771-803

8. **IF TOPIX MODE (buffer flush):** `{SG;0000,YYYY,XXXX,0300,X,` - Graphics command with TOPIX data (line 790)
   - Position X: 0000
   - Position Y: Last line number sent
   - Width: cupsBytesPerLine * 8 (in dots)
   - Height: 300 (fixed height segment)
   - Mode: 3 (TOPIX compression)
   - Followed by: 2-byte big-endian length + compressed data + `|}\n`

## End Page Stage

### EndPage() Function - Lines 428-637

9. **IF TOPIX MODE:** Final buffer flush via `TOPIXCompressOutputBuffer()` (line 466)

10. **IF HEX MODE:** `|}\n` - Close graphics data (line 468)

11. **IF CANCELED:** `{WR|}` - RAM clear/reset (line 476)

12. **IF NOT CANCELED:** `{XS;I,XXXX,XXXXXXXXX|}` - Execute print command (line 596)
    - Subcommand: I (immediate print)
    - Copies: 0001-9999
    - Combined parameter (9 digits):
      - Cut quantity (3 digits): 000-999
      - Media tracking/detect (1 digit): 0-4
      - Print mode (1 char): C=tear-off, D=peel-off, E=rewind
      - Print speed (1 char): 2-A (2,3,4,5,6,8,A=10)
      - Media type (1 digit): 0=direct thermal, 1=thermal transfer, 2=ribbon saving
      - Mirror print (1 digit): 0=normal, 1=mirror
      - Status response (1 digit): 0=no status, 1=with status

13. **IF CUTTER ACTIVE:** `{IB|}` - Immediate cut command (line 600)

14. **TCP WORKAROUND:** 1024 spaces - Padding to avoid TCP zero-window error (line 604)

15. **DUMMY DATA:** 600 null bytes - Workaround for BEV4T last TCP packet lost bug (line 610)

## End Job Stage

No specific commands sent at end of job in the old implementation.

## Notes

- **TOPIX Compression:** Mode 3, uses differential XOR compression with 3-level hierarchical indexing (8x9x9 byte structure)
- **HEX Modes:** Mode 1 (AND), Mode 5 (OR) - raw 8-bit graphics data
- **Coordinate System:** All dimensions in 0.1mm units (100 = 10.0mm)
- **Graphics Position:** Always starts at 0000,0000 (top-left corner)
- **Buffer Management:** TOPIX buffer is 0xFFFF (65535) bytes, flushed when near capacity
- **Line Order:** Graphics sent line by line from top to bottom (y=0 to y=height-1)
