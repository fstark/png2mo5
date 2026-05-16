# k7tool — Detailed Implementation (Phases 4–5: Create Mode)

Enough detail to implement blindly. No ambiguity left.

---

## Phase 4: Create Mode — Generator

### 4.1 Argument Parsing

#### CLI Syntax

```
k7tool -o <output.k7> <file:load:exec> [<file:load:exec> ...]
```

Mode detection already exists: scan `argv` for `-o`. When found:
- The next argument is the output path
- All remaining positional arguments are `file:load:exec` entries

#### Data Structure

```cpp
struct FileEntry {
    std::string path;         // input file path
    uint16_t load_address;    // 16-bit load address
    uint16_t exec_address;    // 16-bit exec address
};

struct CreateArgs {
    std::string output_path;
    std::vector<FileEntry> entries;
};
```

#### Algorithm: Parse Create Arguments

```
Input:  argc, argv
Output: CreateArgs or error

1. Scan argv[1..] for "-o"
2. If found at position i:
   - output_path = argv[i+1]  (error if missing)
   - Collect all other positional args as file entries
3. For each positional arg:
   - Split on ':' into exactly 3 parts (error otherwise)
   - parts[0] = file path
   - parts[1] = load address (must start with "0x" or "0X")
   - parts[2] = exec address (must start with "0x" or "0X")
4. If no file entries → error
```

```cpp
std::optional<CreateArgs> parse_create_args(int argc, char* argv[]);
```

#### Algorithm: Parse Hex Address

```
Input:  string like "0x6000"
Output: uint16_t or error

1. Check starts with "0x" or "0X" → error if not
2. Parse remaining chars as hex → error if invalid chars or empty
3. Check value <= 0xFFFF → error if overflow
4. Return value as uint16_t
```

```cpp
std::optional<uint16_t> parse_hex_address(const std::string& s);
```

**Pitfalls:**
- The colon `:` delimiter could appear in Windows paths (e.g. `C:\foo`). Not a concern on macOS/Linux but worth noting. We split on `:` and require exactly 3 parts.
- Empty path, empty address strings → clear error message.
- "0x" alone (no digits) → error.
- "0x10000" → overflow error.

#### Algorithm: Derive Tape Filename

```
Input:  file path (e.g. "path/to/viewer.bin")
Output: 8-char filename string (space-padded) + 3-char extension "BIN"

1. Extract stem from path (strip directory and extension)
   → "viewer"
2. Convert to uppercase
   → "VIEWER"
3. Truncate to 8 characters if longer
4. Pad with spaces (0x20) to exactly 8 characters
   → "VIEWER  "
5. Extension is always "BIN" (constant)
```

```cpp
std::string derive_tape_filename(const std::string& path);
// Returns exactly 8 characters, uppercase, space-padded.
```

**Pitfalls:**
- Filenames with dots in them (e.g. `image1.mo5z`): `fs::path::stem()` handles this correctly → `"image1"`.
- Empty stem (e.g. `.hidden`): use "        " (8 spaces) — degenerate but valid.
- Non-ASCII characters: pass through as-is (MO5 uses its own charset, but the emulator just stores raw bytes).

### 4.2 K7 Generation

#### Top-Level Algorithm

```
Input:  CreateArgs (output path + list of FileEntry)
Output: writes .k7 file

For each FileEntry:
    1. Read the input file into a byte vector
    2. Derive the tape filename
    3. Build the binary stream (segment header + file data + end marker)
    4. Split the stream into 254-byte chunks
    5. Write: leader + sync + header block
    6. For each chunk: write leader + sync + data block
    7. Write: leader + sync + EOF block

Write all accumulated bytes to the output file.
```

```cpp
void create_k7(const CreateArgs& args);
```

#### Sub-Algorithm: Write Leader + Sync

```
Output bytes:
  44 43 4D 4F 54 4F          "DCMOTO" (6 bytes)
  01 01 01 01 01 01 01 01 01 01   pilot (10 bytes)
  3C 5A                       sync word (2 bytes)

Total: 18 bytes, always identical.
```

```cpp
void write_leader_sync(std::vector<uint8_t>& out);
```

#### Sub-Algorithm: Write Block

```
Input:  block type, data bytes
Output: appended bytes to output vector

1. Write type byte (1 byte)
2. Compute raw_length = data.size() + 2
   - If data.size() == 254 → raw_length = 256 → write 0x00
   - Otherwise → write raw_length as uint8_t
3. Write data bytes
4. Compute checksum = (-sum(data)) & 0xFF
5. Write checksum byte (1 byte)
```

```cpp
void write_block(std::vector<uint8_t>& out, uint8_t type, std::span<const uint8_t> data);
```

**Pitfalls:**
- Length field encoding: when data size is 254, length = 254 + 2 = 256 = 0x00 (wraps). This is the standard "full block" encoding.
- Data size MUST NOT exceed 254 bytes. Caller's responsibility.
- For EOF block: data is empty, length = 2, checksum = 0x00.

#### Sub-Algorithm: Build Header Block Data

```
Input:  8-char filename, file_type=0x02, data_mode=0x00
Output: 14-byte vector

Bytes:
  [0..7]   filename (8 bytes, space-padded)
  [8..10]  "BIN" (3 bytes: 0x42, 0x49, 0x4E)
  [11]     0x02 (binary file type)
  [12]     0x00 (binary data mode)
  [13]     0x00 (auxiliary)
```

```cpp
std::vector<uint8_t> build_header_data(const std::string& filename_8);
// filename_8 must be exactly 8 characters.
```

#### Sub-Algorithm: Build Binary Stream

```
Input:  file data bytes, load address, exec address
Output: complete binary stream (segment + data + end marker)

Stream layout:
  [0x00]                 segment marker
  [size_hi] [size_lo]   segment size = file data length (big-endian)
  [load_hi] [load_lo]   load address (big-endian)
  [...file data...]      raw file bytes
  [0xFF]                 end-of-stream marker
  [0x00] [0x00]         reserved
  [exec_hi] [exec_lo]   exec address (big-endian)

Total stream size = 5 + file_data.size() + 5 = file_data.size() + 10
```

```cpp
std::vector<uint8_t> build_binary_stream(std::span<const uint8_t> file_data,
                                          uint16_t load_addr, uint16_t exec_addr);
```

**Pitfalls:**
- Segment size is the file data length, NOT the stream length. Just the raw code/data bytes.
- If file is larger than 65535 bytes, segment size overflows 16 bits. Error out in this case (MO5 has 48KB RAM max anyway).
- Empty file (0 bytes): technically valid — segment size = 0, no data bytes. The stream is just the 10-byte framing.

#### Sub-Algorithm: Split Stream into Data Blocks

```
Input:  binary stream (arbitrary length)
Output: list of chunks, each ≤ 254 bytes

for i in range(0, stream.size(), 254):
    chunk = stream[i : min(i+254, stream.size())]
    emit chunk
```

This is trivial slicing. Each chunk becomes one data block's payload.

```cpp
std::vector<std::vector<uint8_t>> split_into_chunks(std::span<const uint8_t> stream);
```

#### Sub-Algorithm: Write One Tape File

Combines all the above for a single file entry:

```
Input:  file data, tape filename, load addr, exec addr
Output: appended bytes to output vector

1. Write leader + sync
2. Build header data (14 bytes)
3. Write block(type=0x00, data=header_data)
4. Build binary stream
5. Split stream into chunks
6. For each chunk:
   a. Write leader + sync
   b. Write block(type=0x01, data=chunk)
7. Write leader + sync
8. Write block(type=0xFF, data=empty)
```

```cpp
void write_tape_file(std::vector<uint8_t>& out, const std::vector<uint8_t>& file_data,
                     const std::string& tape_filename, uint16_t load_addr, uint16_t exec_addr);
```

### 4.3 Output & Error Handling

#### Writing the Output File

```cpp
void write_file_bytes(const std::string& path, std::span<const uint8_t> data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) { error and exit }
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
}
```

#### Error Cases

| Condition | Message | Exit code |
|-----------|---------|-----------|
| Missing `-o` value | `Error: -o requires an output path` | 1 |
| No file entries | `Error: no input files specified` | 1 |
| Bad file:load:exec syntax | `Error: invalid entry 'X' (expected file:load:exec)` | 1 |
| Hex parse failure | `Error: invalid address 'X' (expected 0x0000–0xFFFF)` | 1 |
| Input file not found | `Error: cannot open 'X'` | 1 |
| File too large (>65535) | `Error: 'X' is too large (max 65535 bytes)` | 1 |
| Output write failure | `Error: cannot write 'X'` | 1 |

All errors go to stderr. No partial output on error.

### 4.4 Integration with main()

Update the existing `main()`:

```cpp
// In main():
// If -o found → parse create args → call create_k7()
// Otherwise → inspect mode (already implemented)
```

---

## Phase 5: Create Mode — Tests

### 5.1 Filename Derivation Tests

#### Test: Simple Name

```
Input:  "viewer.bin"
Output: "VIEWER  "  (8 chars, space-padded)
```

#### Test: Long Name (truncation)

```
Input:  "longfilename.bin"
Output: "LONGFILE"  (truncated to 8)
```

#### Test: Short Name

```
Input:  "a.bin"
Output: "A       "  (padded to 8)
```

#### Test: Path Stripping

```
Input:  "/home/user/project/viewer.bin"
Output: "VIEWER  "
```

#### Test: Mixed Case

```
Input:  "MyImage.mo5z"
Output: "MYIMAGE "
```

#### Test: Dotted Stem

```
Input:  "image1.mo5z"
Output: "IMAGE1  "
```

### 5.2 Hex Address Parsing Tests

#### Test: Valid Address

```
parse_hex_address("0x6000") → 0x6000
parse_hex_address("0x0000") → 0x0000
parse_hex_address("0xFFFF") → 0xFFFF
parse_hex_address("0X2800") → 0x2800  (uppercase prefix)
```

#### Test: Invalid Address

```
parse_hex_address("6000")    → nullopt (no 0x prefix)
parse_hex_address("0x")      → nullopt (no digits)
parse_hex_address("0xGGGG")  → nullopt (invalid hex)
parse_hex_address("0x10000") → nullopt (overflow)
parse_hex_address("")         → nullopt (empty)
```

### 5.3 Block Writing Tests

#### Test: Write Block with Known Data

```
data = {0x46, 0x49, 0x4C, 0x4C, 0x53, 0x43, 0x52, 0x20,
        0x42, 0x49, 0x4E, 0x02, 0x00, 0x00}
type = 0x00

Expected output bytes:
  [0x00]     type
  [0x10]     length = 16 (14 data + 2)
  [14 data bytes]
  [0xF6]     checksum
```

Verify by parsing back with `read_block()`.

#### Test: Write Full Block (254 bytes)

```
data = 254 bytes
Expected length byte = 0x00 (256 wraps to 0)
```

#### Test: Write EOF Block

```
data = [] (empty)
Expected: [0xFF, 0x02, 0x00]
```

### 5.4 Binary Stream Building Tests

#### Test: Small File

```
file_data = {0x8E, 0x00, 0x00, 0x86, 0xFF, 0xA7, 0x80,
             0x8C, 0x20, 0x00, 0x26, 0xF9, 0x39}  (13 bytes)
load = 0x6000, exec = 0x6000

Expected stream:
  [0x00]           segment marker
  [0x00, 0x0D]    size = 13
  [0x60, 0x00]    load addr
  [13 code bytes]
  [0xFF]           end marker
  [0x00, 0x00]    reserved
  [0x60, 0x00]    exec addr

Total: 23 bytes
```

#### Test: Empty File

```
file_data = {} (0 bytes)
load = 0x2000, exec = 0x2000

Expected stream:
  [0x00, 0x00, 0x00, 0x20, 0x00]  segment: size=0, load=$2000
  [0xFF, 0x00, 0x00, 0x20, 0x00]  end: exec=$2000

Total: 10 bytes
```

### 5.5 Chunk Splitting Tests

#### Test: Stream Fits in One Block

```
stream = 23 bytes
split_into_chunks(stream) → [{23 bytes}]
```

#### Test: Exact Boundary

```
stream = 254 bytes
split_into_chunks(stream) → [{254 bytes}]
```

#### Test: Straddles Boundary

```
stream = 255 bytes
split_into_chunks(stream) → [{254 bytes}, {1 byte}]
```

#### Test: Multiple Full Blocks

```
stream = 762 bytes (254 × 3)
split_into_chunks(stream) → [{254}, {254}, {254}]
```

#### Test: Multiple Blocks + Remainder

```
stream = 600 bytes
split_into_chunks(stream) → [{254}, {254}, {92}]
```

### 5.6 Round-Trip Tests

#### Test: Generate and Parse Back — Single File

```
1. Create a small binary (the fillscr code: 13 bytes)
2. Call create_k7() with load=0x6000, exec=0x6000
3. Read back the generated .k7
4. Parse with parse_tape()
5. Verify:
   - 1 file found
   - filename == "FILLSCR " (or whatever derived)
   - file_type == binary
   - 1 segment: load=0x6000, size=13
   - end_of_stream: exec=0x6000
   - All checksums valid
```

```cpp
TEST_CASE("round-trip: single file", "[create][roundtrip]") {
    std::vector<uint8_t> code = {0x8E,0x00,0x00,0x86,0xFF,0xA7,0x80,
                                  0x8C,0x20,0x00,0x26,0xF9,0x39};
    
    // Create in memory
    std::vector<uint8_t> k7_data;
    write_tape_file(k7_data, code, "FILLSCR ", 0x6000, 0x6000);
    
    // Parse back
    auto result = parse_tape(k7_data);
    REQUIRE(result.files.size() == 1);
    auto& f = result.files[0];
    REQUIRE(f.header.filename == "FILLSCR ");
    REQUIRE(f.header.file_type == FILE_TYPE_BINARY);
    REQUIRE(f.segments.size() == 1);
    REQUIRE(f.segments[0].load_address == 0x6000);
    REQUIRE(f.segments[0].size == 13);
    REQUIRE(f.end_of_stream.has_value());
    REQUIRE(f.end_of_stream->exec_address == 0x6000);
    // All checksums valid
    for (auto& block : f.blocks) REQUIRE(block.checksum_ok);
}
```

#### Test: Generate and Parse Back — Multi-File

```
1. Create two files on one tape:
   - file A: 100 bytes, load=0x2000, exec=0x2000
   - file B: 300 bytes, load=0x4000, exec=0x4000
2. Generate .k7
3. Parse
4. Verify 2 files, correct boundaries, correct addresses
```

```cpp
TEST_CASE("round-trip: multi-file tape", "[create][roundtrip]") {
    std::vector<uint8_t> file_a(100, 0xAA);
    std::vector<uint8_t> file_b(300, 0xBB);
    
    std::vector<uint8_t> k7_data;
    write_tape_file(k7_data, file_a, "FILEA   ", 0x2000, 0x2000);
    write_tape_file(k7_data, file_b, "FILEB   ", 0x4000, 0x4000);
    
    auto result = parse_tape(k7_data);
    REQUIRE(result.files.size() == 2);
    
    REQUIRE(result.files[0].header.filename == "FILEA   ");
    REQUIRE(result.files[0].segments[0].load_address == 0x2000);
    REQUIRE(result.files[0].segments[0].size == 100);
    REQUIRE(result.files[0].end_of_stream->exec_address == 0x2000);
    
    REQUIRE(result.files[1].header.filename == "FILEB   ");
    REQUIRE(result.files[1].segments[0].load_address == 0x4000);
    REQUIRE(result.files[1].segments[0].size == 300);
    REQUIRE(result.files[1].end_of_stream->exec_address == 0x4000);
    
    // All checksums
    for (auto& f : result.files)
        for (auto& b : f.blocks)
            REQUIRE(b.checksum_ok);
}
```

#### Test: Large File (Multiple Full Blocks)

```
1. Create a file of exactly 762 bytes (3 full blocks worth of stream: 762+10=772 stream bytes → 4 blocks)
   Actually: stream = 5 + 762 + 5 = 772 bytes → ceil(772/254) = 4 chunks (254+254+254+10)
2. Generate .k7
3. Parse
4. Verify segment size == 762, correct block count
```

### 5.7 Byte-Level Comparison Test

#### Test: Compare with Known-Good Sample

```
1. Read samples/fillscr-mo5.k7
2. Generate equivalent: 13 bytes of fillscr code, load=0x6000, exec=0x6000
3. Compare generated bytes with sample bytes
4. They should be byte-for-byte identical
```

```cpp
TEST_CASE("byte-level match with fillscr-mo5.k7", "[create][golden]") {
    // The fillscr code
    std::vector<uint8_t> code = {0x8E,0x00,0x00,0x86,0xFF,0xA7,0x80,
                                  0x8C,0x20,0x00,0x26,0xF9,0x39};
    
    // Generate
    std::vector<uint8_t> generated;
    write_tape_file(generated, code, "FILLSCR ", 0x6000, 0x6000);
    
    // Load sample
    auto sample = read_file_bytes("samples/fillscr-mo5.k7");
    
    REQUIRE(generated.size() == sample.size());
    REQUIRE(generated == sample);
}
```

**Pitfall:** This test is the ultimate gold standard. If it passes, the generator is correct. If it fails, hexdump both and diff to find the discrepancy.

### 5.8 Argument Parsing Tests

#### Test: Valid Arguments

```
argv = {"k7tool", "-o", "out.k7", "file.bin:0x6000:0x6000"}
→ output_path = "out.k7", entries = [{path="file.bin", load=0x6000, exec=0x6000}]
```

#### Test: Multiple Files

```
argv = {"k7tool", "-o", "out.k7", "a.bin:0x2000:0x2000", "b.bin:0x4000:0x4000"}
→ entries.size() == 2
```

#### Test: -o at End

```
argv = {"k7tool", "a.bin:0x2000:0x2000", "-o", "out.k7"}
→ Still works (position of -o is flexible)
```

#### Test: Missing -o Value

```
argv = {"k7tool", "-o"}
→ error
```

#### Test: No File Entries

```
argv = {"k7tool", "-o", "out.k7"}
→ error
```

#### Test: Bad Entry Format

```
argv = {"k7tool", "-o", "out.k7", "file.bin:0x6000"}  (only 2 parts)
→ error
```

---

## Function Signatures Summary (Create Mode)

```cpp
// --- Argument parsing ---
std::optional<uint16_t> parse_hex_address(const std::string& s);
std::string derive_tape_filename(const std::string& path);
std::optional<CreateArgs> parse_create_args(int argc, char* argv[]);

// --- Generation primitives ---
void write_leader_sync(std::vector<uint8_t>& out);
void write_block(std::vector<uint8_t>& out, uint8_t type, std::span<const uint8_t> data);
std::vector<uint8_t> build_header_data(const std::string& filename_8);
std::vector<uint8_t> build_binary_stream(std::span<const uint8_t> file_data,
                                          uint16_t load_addr, uint16_t exec_addr);
std::vector<std::vector<uint8_t>> split_into_chunks(std::span<const uint8_t> stream);

// --- High-level ---
void write_tape_file(std::vector<uint8_t>& out, const std::vector<uint8_t>& file_data,
                     const std::string& tape_filename, uint16_t load_addr, uint16_t exec_addr);
void create_k7(const CreateArgs& args);

// --- File I/O ---
void write_file_bytes(const std::string& path, std::span<const uint8_t> data);
```

---

## Implementation Order

Recommended order to enable incremental testing:

1. `parse_hex_address()` — pure function, trivial
2. `derive_tape_filename()` — pure function, uses `std::filesystem`
3. `write_leader_sync()` — trivial, just appends constant bytes
4. `write_block()` — uses `verify_checksum` from Phase 2 to self-check
5. `build_header_data()` — trivial construction
6. `build_binary_stream()` — trivial construction
7. `split_into_chunks()` — trivial slicing
8. `write_tape_file()` — combines 3+4+5+6+7
9. `parse_create_args()` — CLI logic
10. `create_k7()` — orchestrates 8+9, adds file I/O
11. Wire up `main()` — call `create_k7` when `-o` detected

---

## Pitfalls & Gotchas Checklist (Create Mode)

1. **Length=256 encodes as 0x00.** When data is 254 bytes: length = 254+2 = 256. Write `0x00` as the length byte. Don't write `0x00` for other reasons.

2. **Checksum is over DATA bytes only.** Type and length are not included. `checksum = (-sum(data)) & 0xFF`.

3. **Leader before EVERY block.** Including the first one. Each block (header, data, EOF) gets its own separate leader + sync sequence.

4. **Stream includes 5-byte segment header + 5-byte end trailer.** A file of N bytes produces a stream of N+10 bytes total.

5. **Big-endian everywhere.** Addresses and sizes: high byte first. `uint16_t val` → `{val >> 8, val & 0xFF}`.

6. **Filename is exactly 8 bytes.** No null terminator. Space-pad (`0x20`). The `derive_tape_filename` function must guarantee this.

7. **EOF block is always `[0xFF, 0x02, 0x00]`.** Type=0xFF, length=2, checksum=0x00. No data bytes.

8. **File size limit: 65535 bytes.** The segment size field is 16-bit. Files larger than this cannot be represented as a single segment. Error out rather than silently truncating.

9. **Binary mode flag.** `std::ios::binary` is essential when writing the output file. Without it, LF→CRLF translation could corrupt the output on certain platforms.

10. **Don't modify the output file on error.** Build the complete tape in memory first (`std::vector<uint8_t>`), then write atomically at the end. If any input file fails to open, the output is never created.

11. **The golden test is definitive.** If `fillscr-mo5.k7` matches byte-for-byte, the generator is correct. If it doesn't match, the discrepancy reveals the bug.

12. **Chunk boundaries are pure slicing.** The stream is split at 254-byte intervals with no regard for segment markers. Segments can straddle chunk boundaries — this is correct and expected.

13. **Extension in header is "BIN" (0x42, 0x49, 0x4E).** Hardcoded. All files are binary type.

14. **Parse positions: `-o` can appear anywhere.** Don't assume it's `argv[1]`. Scan all of argv.

15. **Round-trip validates everything.** The existing `parse_tape()` + `decode_binary_stream()` serves as the verifier for the generator. If generate → parse → verify passes, the generator is correct.
