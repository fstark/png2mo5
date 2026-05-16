# k7tool — Detailed Implementation (Phases 1–3)

Enough detail to implement blindly. No ambiguity left.

---

## Phase 1: Project Skeleton

### File Layout

```
k7tool/
  Makefile
  k7tool.cpp
  test_k7tool.cpp
  catch2/              ← symlink to ../png2mo5/catch2
  samples/             ← already exists
  DESIGN.md
  K7-FORMAT.md
  PLAN.md
  PLAN_DETAILED.md
```

### Makefile

```makefile
CXX      ?= g++
CXXFLAGS ?= -std=c++23 -O2 -Wall -Wextra

k7tool: k7tool.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

test_k7tool: test_k7tool.cpp k7tool.cpp catch2/catch_amalgamated.cpp
	$(CXX) $(CXXFLAGS) -DK7TOOL_TESTING -o $@ test_k7tool.cpp catch2/catch_amalgamated.cpp

test: test_k7tool
	./test_k7tool

clean:
	rm -f k7tool test_k7tool

.PHONY: test clean
```

Pattern: same as png2mo5/mo5z. The `-DK7TOOL_TESTING` flag excludes `main()` when compiling for tests (the test file `#include`s `k7tool.cpp` directly).

### k7tool.cpp — Top-level Structure

```cpp
// k7tool — MO5 K7 cassette file inspector & generator
// C++23 single-file implementation.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

// ============================================================================
// Constants
// ============================================================================
// ... (see Phase 2 data structures)

// ============================================================================
// Data Structures
// ============================================================================
// ... (see Phase 2)

// ============================================================================
// Parsing functions (inspect mode)
// ============================================================================
// ... (see Phase 2)

// ============================================================================
// Main
// ============================================================================
#ifndef K7TOOL_TESTING
int main(int argc, char* argv[]) {
    // Mode detection: if "-o" present → create mode, else inspect mode
    // For now, only inspect mode is implemented.
    
    // Parse argv:
    //   k7tool something.k7          → inspect
    //   k7tool -o out.k7 f:l:e ...   → create (phase 4)
    
    // If no args or "--help": print usage, exit 0
    // If single positional arg ending in ".k7": inspect mode
    // Otherwise: error
}
#endif
```

### test_k7tool.cpp — Scaffold

```cpp
#define K7TOOL_TESTING
#include "k7tool.cpp"
#include "catch2/catch_amalgamated.hpp"

// Tests go here (Phase 3)
```

### Arg Parsing Logic (Inspect Mode Only for Now)

```
Usage: k7tool <file.k7>
       k7tool -o <output.k7> <file:load:exec> [...]
```

Detection algorithm:
1. Skip `argv[0]`
2. If `argc < 2` → print usage, exit 0
3. Scan for `-o` flag. If found → create mode (not yet implemented, print error & exit 1)
4. Otherwise, first non-flag argument is the .k7 file to inspect

### catch2/ Symlink

```bash
cd k7tool && ln -s ../png2mo5/catch2 catch2
```

---

## Phase 2: Inspect Mode — Parser

### Constants

```cpp
constexpr std::array<uint8_t, 6> LEADER_SIG = {'D','C','M','O','T','O'};
constexpr int PILOT_LEN = 10;
constexpr uint8_t PILOT_BYTE = 0x01;
constexpr int LEADER_LEN = 6 + PILOT_LEN;  // 16
constexpr std::array<uint8_t, 2> SYNC_WORD = {0x3C, 0x5A};

constexpr uint8_t BLOCK_TYPE_HEADER = 0x00;
constexpr uint8_t BLOCK_TYPE_DATA   = 0x01;
constexpr uint8_t BLOCK_TYPE_EOF    = 0xFF;

constexpr uint8_t FILE_TYPE_BASIC  = 0x00;
constexpr uint8_t FILE_TYPE_DATA   = 0x01;
constexpr uint8_t FILE_TYPE_BINARY = 0x02;

constexpr uint8_t DATA_MODE_BINARY = 0x00;
constexpr uint8_t DATA_MODE_ASCII  = 0xFF;

constexpr uint8_t SEGMENT_MARKER_DATA = 0x00;
constexpr uint8_t SEGMENT_MARKER_END  = 0xFF;

constexpr int MAX_DATA_PER_BLOCK = 254;
```

### Data Structures

```cpp
// A single parsed block from the tape
struct Block {
    uint8_t type;             // 0x00, 0x01, or 0xFF
    std::vector<uint8_t> data; // payload bytes (excluding type/length/checksum)
    uint8_t checksum;         // stored checksum byte
    bool checksum_ok;         // true if verification passed
    size_t file_offset;       // byte offset in the .k7 file where this block starts (after sync)
};

// Decoded header info
struct TapeFileHeader {
    std::string filename;     // 8 chars, may have trailing spaces
    std::string extension;    // 3 chars
    uint8_t file_type;        // 0x00, 0x01, 0x02
    uint8_t data_mode;        // 0x00 or 0xFF
    uint8_t auxiliary;
};

// A decoded binary segment
struct BinarySegment {
    uint16_t load_address;
    uint16_t size;
    size_t stream_offset;   // byte offset within the concatenated data stream (for display placement)
};

// End-of-stream info
struct BinaryEndOfStream {
    uint16_t exec_address;
    size_t stream_offset;   // byte offset within the concatenated data stream (for display placement)
};

// A logical file on the tape (header + data blocks + EOF)
struct TapeFile {
    int file_number;                           // 1-based
    TapeFileHeader header;
    std::vector<Block> blocks;                 // all blocks including header and EOF
    std::vector<BinarySegment> segments;       // decoded if binary
    std::optional<BinaryEndOfStream> end_of_stream; // decoded if binary
};

// Top-level parse result
struct TapeContents {
    std::vector<TapeFile> files;
    std::vector<std::string> warnings;        // non-fatal issues
};
```

### 2.1 Low-Level Block Parser

#### Algorithm: Find Next Leader

```
Input:  byte buffer, current offset
Output: offset pointing to first byte AFTER the sync word, or nullopt if EOF

1. Scan forward looking for the byte sequence "DCMOTO" (6 bytes)
2. Once found, verify that the next 10 bytes are all 0x01
   - If not all 0x01: this is a false positive. Resume scanning from offset+1.
   - PITFALL: Real files may have fewer pilot bytes or garbage. 
     Be lenient: require "DCMOTO" followed by AT LEAST 1 pilot byte (0x01),
     then look for sync word 0x3C 0x5A in the next ~20 bytes.
     Actually, for simplicity and compatibility: require exactly the
     DCMOTO + pilot + sync pattern as specified.
     DECISION: Strict match (DCMOTO + 10×0x01 + 3C 5A). If a file differs,
     emit a warning and try looser matching.
3. After pilot: expect sync word 0x3C 0x5A
4. Return offset just past the sync word (pointing at block type byte)
```

**Implementation strategy — two-pass approach:**

```cpp
std::optional<size_t> find_next_block_start(std::span<const uint8_t> data, size_t offset);
```

Simplified: scan byte-by-byte for 0x3C 0x5A preceded by at least one 0x01 and preceded by "DCMOTO". But the cleanest approach:

```
while (offset + LEADER_LEN + 2 <= data.size()) {
    if (data[offset..offset+6] == "DCMOTO") {
        // Check pilot bytes
        bool pilot_ok = true;
        for (int i = 0; i < PILOT_LEN; i++)
            if (data[offset + 6 + i] != 0x01) { pilot_ok = false; break; }
        if (pilot_ok) {
            // Check sync
            size_t sync_pos = offset + LEADER_LEN;
            if (data[sync_pos] == 0x3C && data[sync_pos+1] == 0x5A)
                return sync_pos + 2;  // block starts here
        }
    }
    offset++;
}
return std::nullopt;
```

#### Algorithm: Read One Block

```
Input:  data buffer, offset (pointing at type byte, right after sync)
Output: Block struct, new offset past the block

1. Read type byte (1 byte)
2. Read length byte (1 byte) — call this raw_length
3. Compute effective_length = (raw_length == 0) ? 256 : raw_length
   - Data byte count = effective_length - 2
   - PITFALL: If effective_length < 2, this is malformed. Emit warning.
     Set bytes_consumed = 2 (type + length bytes only), return nullopt.
     The caller will then scan for the next leader.
4. Read (effective_length - 2) data bytes
5. Read 1 checksum byte
6. Verify: (sum of data bytes + checksum) & 0xFF == 0
7. Return Block{type, data, checksum, checksum_ok, file_offset}
   bytes_consumed = 1 (type) + 1 (length) + (effective_length - 1) = effective_length + 1
   IMPORTANT: Use effective_length here, NOT raw_length. When raw_length=0, bytes_consumed=257.
```

**Edge cases / pitfalls:**

- **Length=0 means 256**: data count = 254 bytes. This is the normal "full" data block.
- **Length=1**: Would mean -1 data bytes. Malformed. Emit warning, return nullopt with bytes_consumed=2.
- **Length=2**: Zero data bytes + checksum. Valid for EOF block (type=0xFF, length=0x02, checksum=0x00).
- **Truncation**: If `offset + effective_length + 1 > data.size()`, the file is truncated. Read what's available, flag it.

```cpp
struct ParsedBlock {
    Block block;
    size_t bytes_consumed; // how many bytes past the sync word this block occupies
};

std::optional<ParsedBlock> read_block(std::span<const uint8_t> data, size_t offset);
```

#### Checksum Verification

```cpp
bool verify_checksum(std::span<const uint8_t> data_bytes, uint8_t checksum) {
    uint8_t sum = 0;
    for (uint8_t b : data_bytes) sum += b;
    return ((sum + checksum) & 0xFF) == 0;
}
```

### 2.2 Multi-File Iteration

#### Algorithm: Parse Entire Tape

```
Input:  entire .k7 file as byte vector
Output: TapeContents (list of TapeFiles + warnings)

offset = 0
file_number = 1

loop:
    block_start = find_next_block_start(data, offset)
    if not found → break (end of tape)
    
    parsed = read_block(data, block_start)
    if not parsed → offset = block_start + 1; continue  // corrupted, skip ahead
    block = parsed.block
    
    if block.type == HEADER (0x00):
        Start a new TapeFile
        tape_file.file_number = file_number++
        tape_file.header = decode_header(block.data)
        tape_file.blocks.push_back(block)
        
        // Read subsequent blocks until EOF block
        offset = block_start + parsed.bytes_consumed
        loop:
            next_start = find_next_block_start(data, offset)
            if not found → warning "unexpected end of tape", break
            next_parsed = read_block(data, next_start)
            if not next_parsed → warning "corrupted block", offset = next_start + 1; break
            next_block = next_parsed.block
            tape_file.blocks.push_back(next_block)
            offset = next_start + next_parsed.bytes_consumed
            if next_block.type == EOF (0xFF) → break
        
        // Decode binary stream if file_type == 0x02
        if tape_file.header.file_type == FILE_TYPE_BINARY:
            decode_binary_stream(tape_file)
        
        result.files.push_back(tape_file)
    else:
        // Unexpected block type outside a file context
        warning "orphan block at offset X"
        offset = block_start + parsed.bytes_consumed
```

**Pitfalls:**
- A tape may contain multiple files. After an EOF block, scan for the next leader.
- Some tapes might have garbage between files. The `find_next_block_start` handles this by scanning forward.
- The loop must advance `offset` even on error to avoid infinite loops.

### 2.3 Header Block Decoding

#### Algorithm

```
Input:  Block.data (should be exactly 14 bytes for a standard header)
Output: TapeFileHeader

PITFALL: Length field of header block is 0x10 (16). That means length-2 = 14 data bytes.
If data.size() != 14, emit warning but decode what we can.

filename  = data[0..7]   → 8 bytes, interpret as ASCII, keep spaces
extension = data[8..10]  → 3 bytes, interpret as ASCII
file_type = data[11]     → 0x00=BASIC, 0x01=Data, 0x02=Binary
data_mode = data[12]     → 0x00=binary, 0xFF=ASCII
auxiliary = data[13]     → usually 0x00
```

```cpp
TapeFileHeader decode_header(std::span<const uint8_t> data);
```

**Display helpers:**

```cpp
const char* file_type_str(uint8_t ft) {
    switch (ft) {
        case 0x00: return "BASIC";
        case 0x01: return "data";
        case 0x02: return "binary";
        default:   return "unknown";
    }
}

const char* data_mode_str(uint8_t dm) {
    switch (dm) {
        case 0x00: return "binary";
        case 0xFF: return "ASCII";
        default:   return "unknown";
    }
}
```

### 2.4 Binary Stream Decoding

#### Algorithm

The binary stream is formed by concatenating **all data block payloads** for this file:

```
Input:  Vector of Block (only type==0x01 blocks)
Output: Populates TapeFile.segments and TapeFile.end_of_stream

1. Concatenate: stream = block[0].data + block[1].data + ... + block[N].data
2. Parse stream with a cursor:

cursor = 0
while cursor < stream.size():
    marker = stream[cursor]
    
    if marker == 0x00:  // data segment
        if cursor + 5 > stream.size() → warning "truncated segment header", break
        size_hi = stream[cursor + 1]
        size_lo = stream[cursor + 2]
        segment_size = (size_hi << 8) | size_lo
        addr_hi = stream[cursor + 3]
        addr_lo = stream[cursor + 4]
        load_address = (addr_hi << 8) | addr_lo
        cursor += 5
        
        // Skip over the data bytes (we just record metadata for display)
        if cursor + segment_size > stream.size():
            warning "truncated segment data"
            break
        segments.push_back({load_address, segment_size})
        cursor += segment_size
    
    elif marker == 0xFF:  // end-of-stream
        if cursor + 5 > stream.size() → warning "truncated end marker", break
        // bytes[1..2] are reserved (should be 0x00 0x00)
        exec_hi = stream[cursor + 3]
        exec_lo = stream[cursor + 4]
        exec_address = (exec_hi << 8) | exec_lo
        end_of_stream = {exec_address}
        cursor += 5
        break  // done
    
    else:
        warning "unexpected marker byte 0xXX at stream offset Y"
        break
```

**Pitfalls:**
- Segment data straddles block boundaries. This is why we concatenate ALL data block payloads into one stream FIRST, then parse. Never try to decode segments block-by-block.
- A segment size of 0 is technically valid (empty segment). Handle gracefully.
- If end-of-stream marker is missing (stream ends without 0xFF), emit warning.
- For BASIC files (file_type != 0x02), skip this step entirely.

```cpp
void decode_binary_stream(TapeFile& tape_file);
```

### 2.5 Output Formatting

#### Format Spec

```
=== File 1: "FILLSCR .BIN" ===
  Block 0: Header  type=binary mode=binary
  Block 1: Data    23 bytes   [segment: load=$6000 size=13]  [end: exec=$6000]
  Block 2: EOF
```

For multi-block files:

```
=== File 1: "VIEWER  .BIN" ===
  Block 0: Header  type=binary mode=binary
  Block 1: Data    254 bytes  [segment: load=$2800 size=512]
  Block 2: Data    254 bytes
  Block 3: Data    10 bytes   [end: exec=$2800]
  Block 4: EOF
```

#### Algorithm for Annotation Placement

Segment/end markers need to be placed on the correct data block line. Since segments straddle blocks, we need to track where in the stream each segment and the end-of-stream start.

**Approach:** During `decode_binary_stream`, record the **stream offset** where each segment header and end marker are found (using the `stream_offset` field already in the structs above). Then for display, compute which block each offset falls into.

To map stream offset → block index (within the data blocks only):

```
cumulative = 0
for each data_block (i = 0, 1, ...):   // iterating only type==0x01 blocks
    if stream_offset >= cumulative && stream_offset < cumulative + block.data.size():
        → this segment/marker starts in data_block i
    cumulative += block.data.size()
```

For display: the data_block index `i` maps to `blocks[i + 1]` in the TapeFile (since blocks[0] is the header). So display as "Block {i+1}".

#### Checksum Error Display

If `block.checksum_ok == false`:

```
  Block 3: Data    254 bytes  *** CHECKSUM ERROR (expected=0xAB got=0xCD) ***
```

Compute expected checksum for display:
```cpp
uint8_t expected = (-sum(block.data)) & 0xFF;
```

#### Warning Display

Print warnings at the end of each file, or inline if tied to a specific block.

### Top-Level Inspect Function

```cpp
void inspect(const std::string& path) {
    // 1. Read entire file into vector<uint8_t>
    // 2. Call parse_tape(data) → TapeContents
    // 3. For each TapeFile, print formatted output
    // 4. Print any tape-level warnings
}
```

File reading:

```cpp
std::vector<uint8_t> read_file_bytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open: " + path);
    f.seekg(0, std::ios::end);
    size_t size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(size);
    f.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}
```

---

## Phase 3: Inspect Mode — Tests

### Test File: `test_k7tool.cpp`

```cpp
#define K7TOOL_TESTING
#include "k7tool.cpp"
#include "catch2/catch_amalgamated.hpp"
```

### 3.1 Checksum Tests

#### Test: Good Checksum

```
Given: data = {0x46, 0x49, 0x4C, 0x4C, 0x53, 0x43, 0x52, 0x20,
               0x42, 0x49, 0x4E, 0x02, 0x00, 0x00}
       (This is the "FILLSCR .BIN\x02\x00\x00" header data)
Expected checksum: 0xF6  (since sum = 0x46+0x49+...+0x00 = 0x30A → low byte 0x0A, checksum = -0x0A & 0xFF = 0xF6)
Verify: verify_checksum(data, 0xF6) == true
```

#### Test: Bad Checksum

```
Same data, checksum = 0x00
Verify: verify_checksum(data, 0x00) == false
```

#### Test: EOF Block Checksum

```
data = {} (empty)
checksum = 0x00
Verify: verify_checksum({}, 0x00) == true  (sum=0, 0+0=0)
```

### 3.2 Header Decoding Tests

#### Test: Standard Binary Header

```
Input bytes: 46 49 4C 4C 53 43 52 20  42 49 4E  02  00  00
Expected:
  filename  = "FILLSCR "
  extension = "BIN"
  file_type = 0x02
  data_mode = 0x00
  auxiliary = 0x00
```

#### Test: BASIC Header

```
Input bytes: 50 52 4F 47 20 20 20 20  42 41 53  00  FF  00
Expected:
  filename  = "PROG    "
  extension = "BAS"
  file_type = 0x00
  data_mode = 0xFF
```

#### Test: Short Header (malformed)

```
Input bytes: 46 49 4C 4C  (only 4 bytes)
Expected: decode what we can, no crash. filename = "FILL", rest defaults/empty.
```

### 3.3 Binary Stream Decoding Tests

#### Test: Single Segment

```
stream = [0x00, 0x00, 0x0D, 0x60, 0x00,   ← segment marker, size=13, load=$6000
          <13 dummy bytes>,
          0xFF, 0x00, 0x00, 0x60, 0x00]    ← end marker, exec=$6000

Expected:
  segments = [{load=0x6000, size=13}]
  end_of_stream = {exec=0x6000}
```

#### Test: Multi-Segment

```
stream = [0x00, 0x00, 0x04, 0x20, 0x00,   ← segment 1: size=4, load=$2000
          0xAA, 0xBB, 0xCC, 0xDD,          ← 4 data bytes
          0x00, 0x00, 0x03, 0x40, 0x00,    ← segment 2: size=3, load=$4000
          0x11, 0x22, 0x33,                 ← 3 data bytes
          0xFF, 0x00, 0x00, 0x60, 0x00]    ← end: exec=$6000

Expected:
  segments = [{load=0x2000, size=4}, {load=0x4000, size=3}]
  end_of_stream = {exec=0x6000}
```

#### Test: Stream Split Across Blocks

Construct a TapeFile with two data blocks where the segment header straddles the boundary:

```
Block A data: [0x00, 0x01, 0x00, 0x60]  ← 4 bytes (segment marker, size_hi=1, size_lo=0, addr_hi=0x60)
Block B data: [0x00, <256 bytes of payload...>, 0xFF, 0x00, 0x00, 0x60, 0x00]

Concatenated stream: [0x00, 0x01, 0x00, 0x60, 0x00, <256 bytes>, 0xFF, 0x00, 0x00, 0x60, 0x00]
Expected: segment={load=0x6000, size=256}, end={exec=0x6000}
```

This verifies that concatenation before parsing handles straddle correctly.

### 3.4 Block Parser Unit Tests

#### Test: Read Block — Normal Data Block

Construct raw bytes representing a block (after sync):
```
bytes = [0x01, 0x05, 0xAA, 0xBB, 0xCC, checksum]
        type=0x01, length=5, data=[0xAA,0xBB,0xCC], checksum = (-sum)&0xFF
```

Verify:
- `block.type == 0x01`
- `block.data == {0xAA, 0xBB, 0xCC}`
- `block.checksum_ok == true`

#### Test: Read Block — Full Block (length=0 means 256)

```
bytes = [0x01, 0x00, <254 data bytes>, checksum]
Verify: block.data.size() == 254
```

#### Test: Read Block — EOF Block

```
bytes = [0xFF, 0x02, 0x00]
type=0xFF, length=2, data=[] (0 data bytes), checksum=0x00
checksum_ok: (0 + 0x00) & 0xFF == 0 → true
```

### 3.5 Leader/Sync Finding Tests

#### Test: Find Leader at Start

```
bytes = "DCMOTO" + 10×0x01 + 0x3C + 0x5A + <block data>
find_next_block_start(bytes, 0) → returns 18 (offset pointing at block type)
```

#### Test: Find Leader with Garbage Prefix

```
bytes = [0x00, 0x00, 0xFF, 0xFF] + "DCMOTO" + 10×0x01 + 0x3C + 0x5A + <block>
find_next_block_start(bytes, 0) → returns 22 (4 + 16 + 2)
```

#### Test: No Leader Found

```
bytes = [0x00, 0x00, 0x00, 0x00]
find_next_block_start(bytes, 0) → returns nullopt
```

### 3.6 Integration: Parse Sample Files

For each file in `samples/`:

```cpp
TEST_CASE("parse samples without crash", "[integration]") {
    for (auto& entry : fs::directory_iterator("samples")) {
        if (entry.path().extension() == ".k7") {
            auto data = read_file_bytes(entry.path().string());
            auto result = parse_tape(data);
            // Must find at least one file
            REQUIRE(result.files.size() >= 1);
            // Each file must have a header block and an EOF block
            for (auto& f : result.files) {
                REQUIRE(f.blocks.size() >= 2);  // at minimum: header + EOF
                REQUIRE(f.blocks.front().type == BLOCK_TYPE_HEADER);
                REQUIRE(f.blocks.back().type == BLOCK_TYPE_EOF);
            }
        }
    }
}
```

**Note:** The test binary needs to find `samples/` relative to its working directory. The Makefile runs `./test_k7tool` from the `k7tool/` directory, so `samples/` is a relative path that works.

### 3.7 Integration: Verify Checksums

```cpp
TEST_CASE("all sample checksums pass", "[integration]") {
    for (auto& entry : fs::directory_iterator("samples")) {
        if (entry.path().extension() == ".k7") {
            auto data = read_file_bytes(entry.path().string());
            auto result = parse_tape(data);
            for (auto& f : result.files) {
                for (auto& block : f.blocks) {
                    INFO("File: " << entry.path().filename()
                         << " block at offset " << block.file_offset);
                    REQUIRE(block.checksum_ok);
                }
            }
        }
    }
}
```

### 3.8 Integration: Spot-Check Known Values

```cpp
TEST_CASE("fillscr-mo5.k7 addresses", "[integration]") {
    auto data = read_file_bytes("samples/fillscr-mo5.k7");
    auto result = parse_tape(data);
    REQUIRE(result.files.size() >= 1);
    
    auto& file = result.files[0];
    REQUIRE(file.header.filename == "FILLSCR ");  // or trimmed — decide convention
    REQUIRE(file.header.file_type == FILE_TYPE_BINARY);
    REQUIRE(file.segments.size() >= 1);
    REQUIRE(file.segments[0].load_address == 0x6000);
    REQUIRE(file.end_of_stream.has_value());
    REQUIRE(file.end_of_stream->exec_address == 0x6000);
}
```

**Pitfall:** The expected values come from the K7-FORMAT.md example. If the actual `fillscr-mo5.k7` sample differs, adjust. Run the parser on the sample first and inspect output to confirm.

---

## Function Signatures Summary

```cpp
// --- Low-level ---
bool verify_checksum(std::span<const uint8_t> data, uint8_t checksum);
std::optional<size_t> find_next_block_start(std::span<const uint8_t> tape, size_t offset);
std::optional<ParsedBlock> read_block(std::span<const uint8_t> tape, size_t offset);

// --- Decoding ---
TapeFileHeader decode_header(std::span<const uint8_t> data);
void decode_binary_stream(TapeFile& tape_file);

// --- Top-level parse ---
TapeContents parse_tape(std::span<const uint8_t> data);

// --- Output ---
void print_tape(const TapeContents& tape);

// --- File I/O ---
std::vector<uint8_t> read_file_bytes(const std::string& path);

// --- Entry point ---
void inspect(const std::string& path);
```

---

## Pitfalls & Gotchas Checklist

1. **Length=0 means 256.** Never compute `length - 2` without handling this case first: if `raw_length == 0`, effective length = 256.

2. **Checksum covers DATA only.** The type and length bytes are NOT included in the checksum sum.

3. **Binary stream is cross-block.** Must concatenate all data block payloads FIRST, then parse segments. A segment header can start in one block and end in the next.

4. **EOF block is always empty.** For binary files the exec address is inside the data stream (0xFF marker), NOT in the EOF block. Don't look for addresses in the EOF block.

5. **Multiple files on one tape.** After an EOF block, scan forward for the next leader to find additional files. Don't stop at the first EOF.

6. **Big-endian everywhere.** All 16-bit values (segment size, load address, exec address) are MSB-first.

7. **Filename is exactly 8 bytes.** Space-padded. Don't trim for storage; trim only for display if desired. The raw 8 bytes are what matters for comparison.

8. **Scan for "DCMOTO" byte by byte.** The signature can appear at any offset (e.g., after garbage or after a previous EOF block). Linear scan is fine — K7 files are tiny (tens of KB at most).

9. **`std::span` from `std::vector<uint8_t>`**: Construct with `std::span<const uint8_t>(vec)` or just pass the vector where span is expected (implicit conversion in C++20/23).

10. **File I/O**: Open in binary mode (`std::ios::binary`). Without this, Windows/macOS may mangle `0x1A` or `0x0D 0x0A` bytes.

11. **Relative path for samples in tests**: The test binary runs from `k7tool/` directory. Use `"samples/fillscr-mo5.k7"` etc. as relative paths.

12. **No heap allocation in hot path**: Not a concern — K7 files are tiny. Use vectors freely.

13. **Graceful degradation**: If a block is truncated (file ends mid-block), store what we have, set `checksum_ok = false`, emit warning, and stop parsing that file. Don't crash.

14. **BASIC files in samples**: Some sample .k7 files contain BASIC programs (file_type=0x00), not binaries. The parser must not attempt binary stream decoding on these — only decode segments when `file_type == 0x02`. The integration tests must handle both types gracefully.

15. **`read_block` returns optional**: If the block can't be read (truncated file, length<2), return `std::nullopt`. The caller must check and handle this — either skip to next leader or stop parsing.

---

## Implementation Order Within Phase 2

Recommend implementing in this exact order to enable incremental testing:

1. `read_file_bytes()` — trivial, no dependencies
2. `verify_checksum()` — trivial, pure function
3. `find_next_block_start()` — needs only constants
4. `read_block()` — uses `verify_checksum`
5. `decode_header()` — uses nothing
6. `parse_tape()` — orchestrates 3+4+5, produces TapeContents without binary decoding
7. `decode_binary_stream()` — final piece, called from within `parse_tape` for binary files
8. `print_tape()` + `inspect()` — output layer

After step 6, you can already run against sample files and verify blocks parse correctly. Step 7 adds the segment metadata. Step 8 is pure presentation.
