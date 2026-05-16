# Thomson MO5 .k7 Cassette File Format

Reference document for generating `.k7` files compatible with the DCMO5 emulator (and other Thomson MO5 emulators using the DCMOTO k7 format).

## Overview

A `.k7` file is a raw byte-stream image of a Thomson MO5 cassette tape. The emulator reads it sequentially byte-by-byte, feeding each byte to the emulated MO5's ROM cassette routines. The format consists of a series of **blocks** separated by **leaders**.

## File Structure

```
[Leader] [Sync] [Block 1: Header]
[Leader] [Sync] [Block 2: Data]
[Leader] [Sync] [Block 3: Data]
...
[Leader] [Sync] [Block N: Data]
[Leader] [Sync] [Block N+1: End-of-File]
```

## Leader (Inter-block Gap)

Each block is preceded by a leader sequence:

| Field        | Bytes | Value                        |
|-------------|-------|------------------------------|
| Signature   | 6     | ASCII `DCMOTO` (literal bytes `44 43 4D 4F 54 4F`) |
| Pilot tone  | 10    | `0x01` repeated 10 times     |

The `DCMOTO` signature is a convention of the DCMOTO family emulators. The MO5 ROM's sync-finding routine ignores non-`0x01` bytes and uses the `0x01` pilot bytes to synchronize before looking for the sync word.

**Total leader: 16 bytes.**

## Sync Word

Immediately after the leader pilot bytes:

| Bytes | Value       |
|-------|-------------|
| 2     | `0x3C 0x5A` |

The MO5 ROM searches for this specific 2-byte pattern to mark the start of a block.

## Block Format

After the sync word, each block has:

| Field     | Size         | Description |
|-----------|-------------|-------------|
| Type      | 1 byte      | Block type: `0x00`=Header, `0x01`=Data, `0xFF`=End |
| Length    | 1 byte      | Total block content size (type + data + checksum). `0x00` means 256. |
| Data      | Length-2 bytes | Block payload |
| Checksum  | 1 byte      | Integrity check |

**Key relationships:**
- Data byte count = Length - 2
- Bytes following the Length byte = Length - 1 (data + checksum)
- When Length = `0x00`, treat as 256 (data = 254 bytes)

## Checksum Calculation

The checksum ensures that:

```
(sum_of_all_data_bytes + checksum) & 0xFF == 0
```

Therefore:

```
checksum = (-sum(data_bytes)) & 0xFF
```

**Important:** Only the DATA bytes participate in the checksum. The Type and Length bytes are NOT included.

## Block Types

### Header Block (Type = 0x00)

The header block identifies the file. Length is always `0x10` (16), giving 14 data bytes:

| Offset | Size | Field     | Description |
|--------|------|-----------|-------------|
| 0      | 8    | Filename  | ASCII, space-padded (`0x20`) |
| 8      | 3    | Extension | `"BAS"`, `"BIN"`, or `"DAT"` |
| 11     | 1    | File type | `0x00`=BASIC, `0x01`=Data, `0x02`=Binary/Machine code |
| 12     | 1    | Data mode | `0x00`=Binary, `0xFF`=ASCII text |
| 13     | 1    | Auxiliary | Set to `0x00` |

### Data Block (Type = 0x01)

Contains the file's actual payload. Maximum data per block: **254 bytes** (Length=`0x00`=256, data=254).

All data blocks for a given file form a **continuous byte stream** when concatenated. The block framing is identical regardless of file type — the difference is what that byte stream contains.

#### BASIC files (file type = 0x00)

The data stream is the raw tokenized BASIC program, split across consecutive blocks. No internal addressing.

#### Binary/Machine code files (file type = 0x02)

The data stream contains an embedded segment structure. The stream is split across data blocks at arbitrary 254-byte boundaries — segment headers can straddle block boundaries.

**Binary data stream format:**

```
[0x00] [size_hi] [size_lo] [addr_hi] [addr_lo] [data bytes...]
[0x00] [size_hi] [size_lo] [addr_hi] [addr_lo] [data bytes...]   (optional: more segments)
[0xFF] [0x00] [0x00] [exec_hi] [exec_lo]                          (end-of-stream)
```

Each segment in the stream:

| Offset | Size | Field        | Description |
|--------|------|--------------|-------------|
| 0      | 1    | Marker       | `0x00` = data segment follows |
| 1-2    | 2    | Segment size | Big-endian. Number of code/data bytes in this segment. |
| 3-4    | 2    | Load address | Big-endian. Destination address in MO5 memory. |
| 5+     | N    | Code/Data    | The actual bytes to store at the load address. |

End-of-stream marker:

| Offset | Size | Field        | Description |
|--------|------|--------------|-------------|
| 0      | 1    | Marker       | `0xFF` = end of binary |
| 1-2    | 2    | Reserved     | `0x00 0x00` |
| 3-4    | 2    | Exec address | Big-endian. Entry point for `EXEC`. |

Multiple segments allow scatter-loading to different addresses.

### End-of-File Block (Type = 0xFF)

Signals end of the tape file. Identical for all file types:
- Length = `0x02` (0 data bytes + checksum)
- Data: empty
- Checksum: `0x00`

This block tells the ROM there are no more tape blocks to read. For binary files, the exec address is in the data stream (the `0xFF` end-of-stream marker), NOT in this block.

## Complete Binary File Example

A program that fills the MO5 screen pixel RAM (`$0000`–`$1FFF`) with all pixels set and returns to BASIC. Loads at `$6000`, executes from `$6000`.

### 6809 Machine Code

```
Address  Hex              Assembly
$6000    8E 00 00         LDX  #$0000       ; start of screen pixel RAM
$6003    86 FF            LDA  #$FF         ; all pixels on
$6005    A7 80            STA  ,X+          ; store and advance
$6007    8C 20 00         CMPX #$2000       ; end of screen pixel RAM
$600A    26 F9            BNE  $6005        ; loop
$600C    39               RTS               ; return to BASIC
```

13 bytes of code: `8E 00 00 86 FF A7 80 8C 20 00 26 F9 39`

### File Layout (hex)

```
=== LEADER 1 ===
44 43 4D 4F 54 4F                "DCMOTO"
01 01 01 01 01 01 01 01 01 01    pilot (10 bytes)

=== SYNC ===
3C 5A

=== HEADER BLOCK ===
Type:     00
Length:   10                 (16 decimal)
Data (14 bytes):
  46 49 4C 4C 53 43 52 20   filename "FILLSCR "
  42 49 4E                   extension "BIN"
  02                         file type = binary
  00                         data mode = binary
  00                         auxiliary = 0
Checksum: F6

=== LEADER 2 ===
44 43 4D 4F 54 4F                "DCMOTO"
01 01 01 01 01 01 01 01 01 01    pilot (10 bytes)

=== SYNC ===
3C 5A

=== DATA BLOCK ===
Type:     01
Length:   19                 (25 decimal = 2 + 23 data bytes)
Data (23 bytes) — the binary stream in one block:
  00                         segment marker (data follows)
  00 0D                      segment size = 13 bytes
  60 00                      load address = $6000
  8E 00 00 86 FF A7 80      (code bytes 1-7)
  8C 20 00 26 F9 39         (code bytes 8-13)
  FF                         end-of-stream marker
  00 00                      reserved
  60 00                      exec address = $6000
Checksum: computed as (-sum(all 23 data bytes)) & 0xFF

=== LEADER 3 ===
44 43 4D 4F 54 4F                "DCMOTO"
01 01 01 01 01 01 01 01 01 01    pilot (10 bytes)

=== SYNC ===
3C 5A

=== END-OF-FILE BLOCK ===
Type:     FF
Length:   02
Data:     (empty)
Checksum: 00
```

### Loading in the Emulator

1. Place the `.k7` file in the `software/` subdirectory
2. In the MO5, type: `LOADM""` — should print "Ok"
3. Type `EXEC &H6000` to run
4. Or use `LOADM"",,R` to load and auto-execute

## MO5 Memory Map (Quick Reference)

| Address Range | Description |
|---------------|-------------|
| `$0000–$1F3F` | Video RAM — pixel (FORME) and color (COULEUR), bank-switched via `$A7C0` bit 0 |
| `$1F40–$1FFF` | System variables (monitor ROM) |
| `$2000–$21FF` | Monitor/application registers |
| `$2200–$9FFF` | User RAM (~32KB — code, data, stack) |
| `$A7C0–$A7C3` | PIA system (keyboard, sound, cassette, video bank) |
| `$A7CC–$A7CF` | PIA extension (joysticks, DAC) |
| `$A7E4–$A7E7` | Gate-array video (VBL, counters) |
| `$B000–$EFFF` | Cartridge ROM (16KB) |
| `$F000–$FFFF` | Monitor ROM (4KB) |

Note: Both pixel and color banks occupy the same address range (`$0000–$1F3F`, 8000 bytes). Bit 0 of the PIA register at `$A7C0` selects which bank is accessible: 0 = color (COULEUR), 1 = pixel (FORME). User RAM starts at `$2200` — the `$2000–$21FF` range is reserved for monitor/application registers.

## Implementation Notes

- **Byte order:** All multi-byte values are big-endian (MSB first), matching the 6809 CPU's native byte order.
- **Block vs stream:** The block framing (leader/sync/type/length/checksum) is the tape-level format. The binary segment structure (0x00 marker/size/addr/data, terminated by 0xFF marker) lives INSIDE the continuous data stream formed by concatenating all data block payloads.
- **Block splitting:** The stream is split into blocks at arbitrary 254-byte boundaries. Segment headers may straddle two blocks — the ROM reassembles the stream transparently.
- **Multiple load regions:** Use multiple `0x00`-prefixed segments in the stream to scatter-load to different addresses.
- **Filename:** Exactly 8 characters, padded with spaces (`0x20`). Uppercase ASCII letters, digits, and spaces only.
- **Extension:** Exactly 3 characters. Use `"BIN"` for binary/machine code files.
- **Leader consistency:** Always use exactly `"DCMOTO" + 10 × 0x01` as the leader for DCMO5 compatibility.
- **End-of-file block is always empty:** For both BASIC and binary files, the type=0xFF block has no payload. The binary exec address is in the data stream, not in the EOF block.

## Pseudocode for K7 Generator

```python
def make_k7_binary(filename, load_addr, code_bytes, exec_addr):
    """Generate a .k7 file for a binary MO5 program."""
    output = bytearray()

    def add_leader():
        output.extend(b'DCMOTO')
        output.extend(b'\x01' * 10)

    def add_sync():
        output.extend(b'\x3C\x5A')

    def checksum(data_bytes):
        return (-sum(data_bytes)) & 0xFF

    def add_block(block_type, data):
        length = len(data) + 2  # type + data + checksum
        if length == 256:
            length = 0  # 0 means 256
        output.append(block_type)
        output.append(length)
        output.extend(data)
        output.append(checksum(data))

    # Build the binary data stream
    stream = bytearray()
    # Data segment(s)
    stream.append(0x00)                       # segment marker
    stream.append(len(code_bytes) >> 8)       # size hi
    stream.append(len(code_bytes) & 0xFF)     # size lo
    stream.append(load_addr >> 8)             # addr hi
    stream.append(load_addr & 0xFF)           # addr lo
    stream.extend(code_bytes)                 # machine code
    # End-of-stream marker
    stream.append(0xFF)                       # end marker
    stream.append(0x00)                       # reserved
    stream.append(0x00)                       # reserved
    stream.append(exec_addr >> 8)             # exec addr hi
    stream.append(exec_addr & 0xFF)           # exec addr lo

    # Header block
    add_leader()
    add_sync()
    header_data = bytearray()
    header_data.extend(filename.upper().ljust(8)[:8].encode('ascii'))
    header_data.extend(b'BIN')
    header_data.append(0x02)  # file type = binary
    header_data.append(0x00)  # data mode = binary
    header_data.append(0x00)  # auxiliary
    add_block(0x00, header_data)

    # Data blocks: split stream into 254-byte chunks
    offset = 0
    while offset < len(stream):
        chunk = stream[offset:offset + 254]
        add_leader()
        add_sync()
        add_block(0x01, chunk)
        offset += 254

    # End-of-file block (type=0xFF, always empty)
    add_leader()
    add_sync()
    add_block(0xFF, bytearray())

    return bytes(output)
```

## Verification

To verify a generated `.k7` file works:
1. Place in the `software/` directory of DCMO5
2. Launch the emulator
3. Click the `[k7]` button and select the file
4. Type `LOADM""` and press Enter — should print "Ok"
5. Type `EXEC &H6000` (or your exec address) to run

Or use `LOADM"",,R` to load and auto-execute.

To reboot the emulator and try again: press **Escape**.
