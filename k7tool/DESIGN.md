# k7tool — MO5 K7 Cassette File Generator & Inspector

Create and inspect `.k7` cassette images for the Thomson MO5.

C++23 single-file implementation. Zero external dependencies.

## Usage

### Create mode

```
k7tool -o output.k7 file:load_addr:exec_addr [file:load_addr:exec_addr ...]
```

Each positional argument is a binary file to place on the tape, with its load and exec addresses (hex, `0x` prefix). All files are written as binary type (0x02) entries.

Example:

```
k7tool -o slideshow.k7 \
  viewer.bin:0x2800:0x2800 \
  image1.mo5z:0x3000:0x2900 \
  image2.mo5z:0x3000:0x2900
```

### Inspect mode

```
k7tool something.k7
```

Parses and dumps the tape structure: blocks, filenames, segment addresses, checksums.

### Mode detection

Implicit. If `-o` is present, create. Otherwise, inspect the input `.k7` file.

## Create Mode

### Input

Each argument has the form `path:load:exec` where:
- `path` — input file (raw binary blob)
- `load` — 16-bit load address (hex with `0x` prefix)
- `exec` — 16-bit exec address (hex with `0x` prefix)

### Output tape structure

```
[Leader][Sync][Header Block: file 1]
[Leader][Sync][Data Block 1]
...
[Leader][Sync][Data Block N]
[Leader][Sync][EOF Block]
[Leader][Sync][Header Block: file 2]
[Leader][Sync][Data Block 1]
...
[Leader][Sync][Data Block N]
[Leader][Sync][EOF Block]
...
```

Each input file becomes one complete tape file (header + data blocks + EOF). Every block is preceded by its own leader + sync.

### Tape filename derivation

- Strip directory and extension from input path
- Uppercase
- Truncate or pad to 8 characters (space-padded)
- Extension is always `BIN`

### Binary stream format (inside data blocks)

For each file, the data stream is:

```
[0x00][size_hi][size_lo][load_hi][load_lo][...file bytes...]
[0xFF][0x00][0x00][exec_hi][exec_lo]
```

One segment per file. The stream is split across data blocks at 254-byte boundaries.

### Validation

- Addresses must parse as valid hex and fit in 16 bits (0x0000–0xFFFF)
- No semantic validation of addresses (VRAM, overlap, etc.)

## Inspect Mode

### Output format

```
=== File 1: "VIEWER  .BIN" ===
Block 0: Header  type=binary mode=binary
Block 1: Data    254 bytes  [segment: load=$2800 size=512]
Block 2: Data    254 bytes
Block 3: Data    10 bytes   [end-of-stream: exec=$2800]
Block 4: EOF

=== File 2: "IMAGE1  .BIN" ===
Block 5: Header  type=binary mode=binary
Block 6: Data    254 bytes  [segment: load=$3000 size=2100]
...
```

### Behavior

- Parses all files on the tape (continues past EOF blocks)
- Verifies checksums — reports mismatches inline
- Best-effort on malformed data — prints warnings, continues parsing
- Decodes binary stream metadata (segment load addresses, sizes, exec address)

## Implementation

- C++23, single `k7tool.cpp`
- No external dependencies
- Makefile with `test` target
- Tests use Catch2 (amalgamated header in `catch2/`)

## Testing Strategy

1. **Unit tests** — checksum calculation, filename derivation, block splitting, address parsing
2. **Parse known-good samples** — verify `samples/*.k7` parse without errors
3. **Round-trip** — generate a .k7, parse it back, verify structure matches
