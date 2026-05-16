# k7tool — Implementation Plan

Inspect first, then create. The parser validates our understanding of the format against real files before we generate anything.

## Phase 1: Project Skeleton

- [ ] `Makefile` (same pattern as png2mo5/mo5z)
- [ ] `k7tool.cpp` with `main()`, arg parsing (detect inspect vs create mode)
- [ ] `test_k7tool.cpp` with Catch2 scaffold
- [ ] Symlink or copy `catch2/` from png2mo5

## Phase 2: Inspect Mode — Parser

Core parsing logic. Read a .k7 file and decompose it into structured data.

### 2.1 Low-level block parser

- [ ] Find leader: scan for `DCMOTO` + pilot bytes
- [ ] Read sync word (0x3C 0x5A)
- [ ] Read block: type, length, data, checksum
- [ ] Verify checksum, flag mismatches
- [ ] Handle truncation/EOF gracefully (best-effort)

### 2.2 Multi-file iteration

- [ ] After an EOF block (type=0xFF), continue scanning for next leader
- [ ] Stop when actual end-of-file reached
- [ ] Track file number for output display

### 2.3 Header block decoding

- [ ] Parse 8-char filename + 3-char extension
- [ ] Decode file type (0x00=BASIC, 0x01=Data, 0x02=Binary)
- [ ] Decode data mode (0x00=binary, 0xFF=ASCII)

### 2.4 Binary stream decoding

- [ ] Concatenate all data block payloads into a continuous stream
- [ ] Parse segment markers (0x00 prefix): size + load address
- [ ] Parse end-of-stream marker (0xFF prefix): exec address
- [ ] Handle segments that straddle block boundaries

### 2.5 Output formatting

- [ ] Print file header line: `=== File N: "NAME    .EXT" ===`
- [ ] Print each block: type, data size, decoded metadata
- [ ] Print checksum errors inline
- [ ] Print warnings for malformed data

## Phase 3: Inspect Mode — Tests

- [ ] Unit test: checksum verification (known good, known bad)
- [ ] Unit test: header decoding (filename, type, mode)
- [ ] Unit test: binary stream decoding (one segment, multi-segment)
- [ ] Integration: parse each file in `samples/` without crash
- [ ] Integration: verify all checksums in `samples/` pass
- [ ] Integration: spot-check decoded addresses against known values (e.g. `fillscr-mo5.k7` should show load=$6000 exec=$6000)

## Phase 4: Create Mode — Generator

### 4.1 Argument parsing

- [ ] Parse `file:load:exec` syntax
- [ ] Validate hex addresses (0x prefix, 16-bit range)
- [ ] Derive tape filename from input path

### 4.2 K7 generation

- [ ] Write leader (DCMOTO + 10×0x01)
- [ ] Write sync (0x3C 0x5A)
- [ ] Write header block (filename, BIN, type=0x02, mode=0x00)
- [ ] Build binary stream: segment marker + size + load addr + data + end marker + exec addr
- [ ] Split stream into 254-byte chunks, emit as data blocks
- [ ] Write EOF block (type=0xFF, length=0x02, checksum=0x00)
- [ ] Repeat for each input file

### 4.3 Output

- [ ] Write all bytes to output file specified by `-o`
- [ ] Error on missing `-o` in create mode

## Phase 5: Create Mode — Tests

- [ ] Unit test: filename derivation (strip path, uppercase, pad/truncate)
- [ ] Unit test: block splitting at 254-byte boundary
- [ ] Unit test: checksum generation matches expected values
- [ ] Round-trip: generate .k7 from known binary, parse it back, verify structure
- [ ] Round-trip: generate multi-file .k7, parse, verify all file boundaries correct
- [ ] Emulator smoke test: generate `fillscr-mo5.k7` equivalent, compare with sample

## Phase 6: Validation Against Emulator

- [ ] Load generated .k7 in DCMO5 emulator
- [ ] Verify LOADM"" succeeds
- [ ] Verify EXEC runs correctly
- [ ] Test multi-file tape (LOADM twice)

## Order of Implementation

```
Phase 1 → Phase 2 → Phase 3 → (checkpoint: inspect is solid)
→ Phase 4 → Phase 5 → Phase 6
```

Each phase is a clean commit point.
