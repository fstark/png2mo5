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

constexpr int MAX_DATA_PER_BLOCK [[maybe_unused]] = 254;

// ============================================================================
// Data Structures
// ============================================================================

struct Block {
    uint8_t type;
    std::vector<uint8_t> data;
    uint8_t checksum;
    bool checksum_ok;
    size_t file_offset;  // byte offset in the .k7 file where this block starts (after sync)
};

struct TapeFileHeader {
    std::string filename;   // 8 chars, may have trailing spaces
    std::string extension;  // 3 chars
    uint8_t file_type;      // 0x00, 0x01, 0x02
    uint8_t data_mode;      // 0x00 or 0xFF
    uint8_t auxiliary;
};

struct BinarySegment {
    uint16_t load_address;
    uint16_t size;
    size_t stream_offset;
};

struct BinaryEndOfStream {
    uint16_t exec_address;
    size_t stream_offset;
};

struct TapeFile {
    int file_number;
    TapeFileHeader header;
    std::vector<Block> blocks;
    std::vector<BinarySegment> segments;
    std::optional<BinaryEndOfStream> end_of_stream;
};

struct TapeContents {
    std::vector<TapeFile> files;
    std::vector<std::string> warnings;
};

struct ParsedBlock {
    Block block;
    size_t bytes_consumed;
};

// ============================================================================
// File I/O
// ============================================================================

std::vector<uint8_t> read_file_bytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "Error: cannot open '%s'\n", path.c_str());
        std::exit(1);
    }
    f.seekg(0, std::ios::end);
    size_t size = static_cast<size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(size);
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(size));
    return buf;
}

// ============================================================================
// Low-level Parsing
// ============================================================================

bool verify_checksum(std::span<const uint8_t> data, uint8_t checksum) {
    uint8_t sum = 0;
    for (uint8_t b : data) sum += b;
    return ((sum + checksum) & 0xFF) == 0;
}

std::optional<size_t> find_next_block_start(std::span<const uint8_t> tape, size_t offset) {
    while (offset + 4 <= tape.size()) {  // minimum: 2 pilot bytes + 2 sync bytes
        // Try strict match: "DCMOTO" + 10 pilot + sync
        if (offset + LEADER_LEN + 2 <= tape.size() &&
            std::equal(LEADER_SIG.begin(), LEADER_SIG.end(), tape.begin() + offset)) {
            bool pilot_ok = true;
            for (int i = 0; i < PILOT_LEN; i++) {
                if (tape[offset + 6 + i] != PILOT_BYTE) {
                    pilot_ok = false;
                    break;
                }
            }
            if (pilot_ok) {
                size_t sync_pos = offset + LEADER_LEN;
                if (sync_pos + 2 <= tape.size() &&
                    tape[sync_pos] == SYNC_WORD[0] && tape[sync_pos + 1] == SYNC_WORD[1]) {
                    return sync_pos + 2;
                }
            }
        }

        // Fallback: at least 2 pilot bytes (0x01) followed by sync word
        if (tape[offset] == PILOT_BYTE &&
            offset + 3 < tape.size() &&
            tape[offset + 1] == PILOT_BYTE &&
            tape[offset + 2] == SYNC_WORD[0] && tape[offset + 3] == SYNC_WORD[1]) {
            return offset + 4;
        }

        offset++;
    }
    return std::nullopt;
}

std::optional<ParsedBlock> read_block(std::span<const uint8_t> tape, size_t offset) {
    // Need at least type + length
    if (offset + 2 > tape.size()) return std::nullopt;

    uint8_t type = tape[offset];
    uint8_t raw_length = tape[offset + 1];

    int effective_length = (raw_length == 0) ? 256 : raw_length;

    if (effective_length < 2) {
        // Malformed: length=1 means -1 data bytes
        return std::nullopt;
    }

    int data_count = effective_length - 2;
    // Total bytes after the length field: (effective_length - 1) = data_count + 1 (checksum)
    size_t total_needed = offset + 2 + data_count + 1;

    Block block;
    block.type = type;
    block.file_offset = offset;

    if (total_needed > tape.size()) {
        // Truncated — read what we can
        size_t available_data = tape.size() - (offset + 2);
        if (available_data > 0) {
            // No checksum available
            block.data.assign(tape.begin() + offset + 2, tape.end());
            block.checksum = 0;
            block.checksum_ok = false;
        } else {
            block.data.clear();
            block.checksum = 0;
            block.checksum_ok = false;
        }
        size_t consumed = tape.size() - offset;
        return ParsedBlock{block, consumed};
    }

    block.data.assign(tape.begin() + offset + 2, tape.begin() + offset + 2 + data_count);
    block.checksum = tape[offset + 2 + data_count];
    block.checksum_ok = verify_checksum(block.data, block.checksum);

    size_t bytes_consumed = 1 + 1 + data_count + 1;  // type + length + data + checksum
    return ParsedBlock{block, bytes_consumed};
}

// ============================================================================
// Header Decoding
// ============================================================================

const char* file_type_str(uint8_t ft) {
    switch (ft) {
        case FILE_TYPE_BASIC:  return "BASIC";
        case FILE_TYPE_DATA:   return "data";
        case FILE_TYPE_BINARY: return "binary";
        default:               return "unknown";
    }
}

const char* data_mode_str(uint8_t dm) {
    switch (dm) {
        case DATA_MODE_BINARY: return "binary";
        case DATA_MODE_ASCII:  return "ASCII";
        default:               return "unknown";
    }
}

TapeFileHeader decode_header(std::span<const uint8_t> data) {
    TapeFileHeader h;
    h.file_type = 0;
    h.data_mode = 0;
    h.auxiliary = 0;

    size_t len = data.size();

    // Filename: bytes 0-7
    if (len >= 8) {
        h.filename = std::string(reinterpret_cast<const char*>(data.data()), 8);
    } else {
        h.filename = std::string(reinterpret_cast<const char*>(data.data()), len);
    }

    // Extension: bytes 8-10
    if (len >= 11) {
        h.extension = std::string(reinterpret_cast<const char*>(data.data() + 8), 3);
    } else if (len > 8) {
        h.extension = std::string(reinterpret_cast<const char*>(data.data() + 8), len - 8);
    }

    // File type: byte 11
    if (len >= 12) h.file_type = data[11];

    // Data mode: byte 12
    if (len >= 13) h.data_mode = data[12];

    // Auxiliary: byte 13
    if (len >= 14) h.auxiliary = data[13];

    return h;
}

// ============================================================================
// Binary Stream Decoding
// ============================================================================

void decode_binary_stream(TapeFile& tape_file) {
    // Concatenate all data block payloads
    std::vector<uint8_t> stream;
    for (const auto& block : tape_file.blocks) {
        if (block.type == BLOCK_TYPE_DATA) {
            stream.insert(stream.end(), block.data.begin(), block.data.end());
        }
    }

    size_t cursor = 0;
    while (cursor < stream.size()) {
        uint8_t marker = stream[cursor];

        if (marker == SEGMENT_MARKER_DATA) {
            if (cursor + 5 > stream.size()) break;  // truncated header

            uint16_t seg_size = (static_cast<uint16_t>(stream[cursor + 1]) << 8) |
                                 static_cast<uint16_t>(stream[cursor + 2]);
            uint16_t load_addr = (static_cast<uint16_t>(stream[cursor + 3]) << 8) |
                                  static_cast<uint16_t>(stream[cursor + 4]);

            BinarySegment seg;
            seg.load_address = load_addr;
            seg.size = seg_size;
            seg.stream_offset = cursor;
            tape_file.segments.push_back(seg);

            cursor += 5;

            if (cursor + seg_size > stream.size()) break;  // truncated data
            cursor += seg_size;

        } else if (marker == SEGMENT_MARKER_END) {
            if (cursor + 5 > stream.size()) break;  // truncated end marker

            uint16_t exec_addr = (static_cast<uint16_t>(stream[cursor + 3]) << 8) |
                                  static_cast<uint16_t>(stream[cursor + 4]);

            BinaryEndOfStream eos;
            eos.exec_address = exec_addr;
            eos.stream_offset = cursor;
            tape_file.end_of_stream = eos;
            cursor += 5;
            break;  // done

        } else {
            // Unexpected marker — stop
            break;
        }
    }
}

// ============================================================================
// Top-Level Parser
// ============================================================================

TapeContents parse_tape(std::span<const uint8_t> data) {
    TapeContents result;
    size_t offset = 0;
    int file_number = 1;

    while (true) {
        auto block_start = find_next_block_start(data, offset);
        if (!block_start) break;

        auto parsed = read_block(data, *block_start);
        if (!parsed) {
            offset = *block_start + 1;
            continue;
        }

        if (parsed->block.type == BLOCK_TYPE_HEADER) {
            TapeFile tape_file;
            tape_file.file_number = file_number++;
            tape_file.header = decode_header(parsed->block.data);
            tape_file.blocks.push_back(parsed->block);

            offset = *block_start + parsed->bytes_consumed;

            // Read subsequent blocks until EOF
            while (true) {
                auto next_start = find_next_block_start(data, offset);
                if (!next_start) {
                    result.warnings.push_back("Unexpected end of tape in file " +
                        std::to_string(tape_file.file_number));
                    break;
                }

                auto next_parsed = read_block(data, *next_start);
                if (!next_parsed) {
                    result.warnings.push_back("Corrupted block in file " +
                        std::to_string(tape_file.file_number));
                    offset = *next_start + 1;
                    break;
                }

                tape_file.blocks.push_back(next_parsed->block);
                offset = *next_start + next_parsed->bytes_consumed;

                if (next_parsed->block.type == BLOCK_TYPE_EOF) break;
            }

            // Decode binary stream if applicable
            if (tape_file.header.file_type == FILE_TYPE_BINARY) {
                decode_binary_stream(tape_file);
            }

            result.files.push_back(std::move(tape_file));
        } else {
            // Orphan block outside a file context
            result.warnings.push_back("Orphan block (type=0x" +
                std::string(1, "0123456789ABCDEF"[(parsed->block.type >> 4) & 0xF]) +
                std::string(1, "0123456789ABCDEF"[parsed->block.type & 0xF]) +
                ") at offset " + std::to_string(*block_start));
            offset = *block_start + parsed->bytes_consumed;
        }
    }

    return result;
}

// ============================================================================
// Output Formatting
// ============================================================================

void print_tape(const TapeContents& tape) {
    for (const auto& file : tape.files) {
        std::printf("=== File %d: \"%s.%s\" ===\n",
                    file.file_number,
                    file.header.filename.c_str(),
                    file.header.extension.c_str());

        // Build a map: for each data block index, list annotations
        // Data blocks are all blocks with type==DATA
        // We need to map segment/end stream_offsets to the data block they fall in
        struct Annotation {
            std::string text;
        };

        // Collect data block sizes for offset mapping
        std::vector<size_t> data_block_indices;  // indices into file.blocks that are data blocks
        std::vector<size_t> data_block_cumulative;  // cumulative byte offset at start of each data block
        size_t cumulative = 0;
        for (size_t i = 0; i < file.blocks.size(); i++) {
            if (file.blocks[i].type == BLOCK_TYPE_DATA) {
                data_block_indices.push_back(i);
                data_block_cumulative.push_back(cumulative);
                cumulative += file.blocks[i].data.size();
            }
        }

        // Map stream_offset → block index in file.blocks
        auto find_block_for_offset = [&](size_t stream_offset) -> std::optional<size_t> {
            for (size_t i = 0; i < data_block_indices.size(); i++) {
                size_t start = data_block_cumulative[i];
                size_t end = start + file.blocks[data_block_indices[i]].data.size();
                if (stream_offset >= start && stream_offset < end) {
                    return data_block_indices[i];
                }
            }
            return std::nullopt;
        };

        // Build annotation strings per block index
        std::vector<std::vector<std::string>> annotations(file.blocks.size());

        for (const auto& seg : file.segments) {
            auto idx = find_block_for_offset(seg.stream_offset);
            if (idx) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "[segment: load=$%04X size=%u]",
                              seg.load_address, seg.size);
                annotations[*idx].push_back(buf);
            }
        }

        if (file.end_of_stream) {
            auto idx = find_block_for_offset(file.end_of_stream->stream_offset);
            if (idx) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "[end: exec=$%04X]",
                              file.end_of_stream->exec_address);
                annotations[*idx].push_back(buf);
            }
        }

        // Print blocks
        for (size_t i = 0; i < file.blocks.size(); i++) {
            const auto& block = file.blocks[i];

            std::printf("  Block %zu: ", i);

            switch (block.type) {
                case BLOCK_TYPE_HEADER:
                    std::printf("Header  type=%s mode=%s",
                                file_type_str(file.header.file_type),
                                data_mode_str(file.header.data_mode));
                    break;

                case BLOCK_TYPE_DATA:
                    std::printf("Data    %zu bytes", block.data.size());
                    break;

                case BLOCK_TYPE_EOF:
                    std::printf("EOF");
                    break;

                default:
                    std::printf("Unknown (0x%02X) %zu bytes", block.type, block.data.size());
                    break;
            }

            // Checksum error
            if (!block.checksum_ok) {
                uint8_t expected = 0;
                for (uint8_t b : block.data) expected += b;
                expected = (-expected) & 0xFF;
                std::printf("  *** CHECKSUM ERROR (expected=0x%02X got=0x%02X) ***",
                            expected, block.checksum);
            }

            // Annotations
            for (const auto& ann : annotations[i]) {
                std::printf("  %s", ann.c_str());
            }

            std::printf("\n");
        }

        std::printf("\n");
    }

    // Print tape-level warnings
    for (const auto& w : tape.warnings) {
        std::printf("WARNING: %s\n", w.c_str());
    }
}

// ============================================================================
// Inspect Mode Entry Point
// ============================================================================

void inspect(const std::string& path) {
    auto data = read_file_bytes(path);
    auto tape = parse_tape(data);
    print_tape(tape);
}

// ============================================================================
// Create Mode — Argument Parsing
// ============================================================================

struct FileEntry {
    std::string path;
    uint16_t load_address;
    uint16_t exec_address;
};

struct CreateArgs {
    std::string output_path;
    std::vector<FileEntry> entries;
};

std::optional<uint16_t> parse_hex_address(const std::string& s) {
    if (s.size() < 3) return std::nullopt;
    if (s[0] != '0' || (s[1] != 'x' && s[1] != 'X')) return std::nullopt;
    
    std::string hex_part = s.substr(2);
    if (hex_part.empty()) return std::nullopt;
    
    unsigned long val = 0;
    try {
        size_t pos = 0;
        val = std::stoul(hex_part, &pos, 16);
        if (pos != hex_part.size()) return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
    
    if (val > 0xFFFF) return std::nullopt;
    return static_cast<uint16_t>(val);
}

std::string derive_tape_filename(const std::string& path) {
    std::string stem = fs::path(path).stem().string();
    // Uppercase
    std::transform(stem.begin(), stem.end(), stem.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    // Truncate or pad to 8 chars
    if (stem.size() > 8) stem.resize(8);
    while (stem.size() < 8) stem.push_back(' ');
    return stem;
}

std::optional<CreateArgs> parse_create_args(int argc, char* argv[]) {
    CreateArgs args;
    
    // Find -o and its value
    int o_index = -1;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-o") == 0) {
            o_index = i;
            break;
        }
    }
    
    if (o_index < 0) return std::nullopt;
    
    if (o_index + 1 >= argc) {
        std::fprintf(stderr, "Error: -o requires an output path\n");
        return std::nullopt;
    }
    args.output_path = argv[o_index + 1];
    
    // Collect file entries (all args except -o and its value)
    for (int i = 1; i < argc; i++) {
        if (i == o_index || i == o_index + 1) continue;
        
        std::string arg = argv[i];
        // Split on ':'
        size_t first_colon = arg.find(':');
        if (first_colon == std::string::npos) {
            std::fprintf(stderr, "Error: invalid entry '%s' (expected file:load:exec)\n", arg.c_str());
            return std::nullopt;
        }
        size_t second_colon = arg.find(':', first_colon + 1);
        if (second_colon == std::string::npos) {
            std::fprintf(stderr, "Error: invalid entry '%s' (expected file:load:exec)\n", arg.c_str());
            return std::nullopt;
        }
        // Check no more colons
        if (arg.find(':', second_colon + 1) != std::string::npos) {
            std::fprintf(stderr, "Error: invalid entry '%s' (expected file:load:exec)\n", arg.c_str());
            return std::nullopt;
        }
        
        FileEntry entry;
        entry.path = arg.substr(0, first_colon);
        std::string load_str = arg.substr(first_colon + 1, second_colon - first_colon - 1);
        std::string exec_str = arg.substr(second_colon + 1);
        
        auto load = parse_hex_address(load_str);
        if (!load) {
            std::fprintf(stderr, "Error: invalid address '%s' (expected 0x0000-0xFFFF)\n", load_str.c_str());
            return std::nullopt;
        }
        auto exec = parse_hex_address(exec_str);
        if (!exec) {
            std::fprintf(stderr, "Error: invalid address '%s' (expected 0x0000-0xFFFF)\n", exec_str.c_str());
            return std::nullopt;
        }
        
        entry.load_address = *load;
        entry.exec_address = *exec;
        args.entries.push_back(entry);
    }
    
    if (args.entries.empty()) {
        std::fprintf(stderr, "Error: no input files specified\n");
        return std::nullopt;
    }
    
    return args;
}

// ============================================================================
// Create Mode — Generation
// ============================================================================

void write_leader_sync(std::vector<uint8_t>& out) {
    out.insert(out.end(), LEADER_SIG.begin(), LEADER_SIG.end());
    for (int i = 0; i < PILOT_LEN; i++) out.push_back(PILOT_BYTE);
    out.push_back(SYNC_WORD[0]);
    out.push_back(SYNC_WORD[1]);
}

void write_block(std::vector<uint8_t>& out, uint8_t type, std::span<const uint8_t> data) {
    out.push_back(type);
    uint8_t raw_length = static_cast<uint8_t>((data.size() + 2) & 0xFF);
    out.push_back(raw_length);
    out.insert(out.end(), data.begin(), data.end());
    // Checksum
    uint8_t sum = 0;
    for (uint8_t b : data) sum += b;
    out.push_back(static_cast<uint8_t>(-sum));
}

std::vector<uint8_t> build_header_data(const std::string& filename_8) {
    std::vector<uint8_t> data;
    data.reserve(14);
    // Filename (8 bytes)
    for (int i = 0; i < 8; i++) data.push_back(static_cast<uint8_t>(filename_8[i]));
    // Extension "BIN"
    data.push_back('B');
    data.push_back('I');
    data.push_back('N');
    // File type = binary
    data.push_back(FILE_TYPE_BINARY);
    // Data mode = binary
    data.push_back(DATA_MODE_BINARY);
    // Auxiliary
    data.push_back(0x00);
    return data;
}

std::vector<uint8_t> build_binary_stream(std::span<const uint8_t> file_data,
                                          uint16_t load_addr, uint16_t exec_addr) {
    std::vector<uint8_t> stream;
    stream.reserve(file_data.size() + 10);
    // Segment header
    stream.push_back(SEGMENT_MARKER_DATA);
    stream.push_back(static_cast<uint8_t>(file_data.size() >> 8));
    stream.push_back(static_cast<uint8_t>(file_data.size() & 0xFF));
    stream.push_back(static_cast<uint8_t>(load_addr >> 8));
    stream.push_back(static_cast<uint8_t>(load_addr & 0xFF));
    // File data
    stream.insert(stream.end(), file_data.begin(), file_data.end());
    // End-of-stream marker
    stream.push_back(SEGMENT_MARKER_END);
    stream.push_back(0x00);
    stream.push_back(0x00);
    stream.push_back(static_cast<uint8_t>(exec_addr >> 8));
    stream.push_back(static_cast<uint8_t>(exec_addr & 0xFF));
    return stream;
}

std::vector<std::vector<uint8_t>> split_into_chunks(std::span<const uint8_t> stream) {
    std::vector<std::vector<uint8_t>> chunks;
    size_t offset = 0;
    while (offset < stream.size()) {
        size_t chunk_size = std::min(static_cast<size_t>(MAX_DATA_PER_BLOCK), stream.size() - offset);
        chunks.emplace_back(stream.begin() + offset, stream.begin() + offset + chunk_size);
        offset += chunk_size;
    }
    return chunks;
}

void write_tape_file(std::vector<uint8_t>& out, const std::vector<uint8_t>& file_data,
                     const std::string& tape_filename, uint16_t load_addr, uint16_t exec_addr) {
    // Header block
    write_leader_sync(out);
    auto header_data = build_header_data(tape_filename);
    write_block(out, BLOCK_TYPE_HEADER, header_data);
    
    // Data blocks
    auto stream = build_binary_stream(file_data, load_addr, exec_addr);
    auto chunks = split_into_chunks(stream);
    for (const auto& chunk : chunks) {
        write_leader_sync(out);
        write_block(out, BLOCK_TYPE_DATA, chunk);
    }
    
    // EOF block
    write_leader_sync(out);
    std::span<const uint8_t> empty;
    write_block(out, BLOCK_TYPE_EOF, empty);
}

void write_file_bytes(const std::string& path, std::span<const uint8_t> data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "Error: cannot write '%s'\n", path.c_str());
        std::exit(1);
    }
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

void create_k7(const CreateArgs& args) {
    std::vector<uint8_t> output;
    
    for (const auto& entry : args.entries) {
        // Read input file
        auto file_data = read_file_bytes(entry.path);
        if (file_data.size() > 65535) {
            std::fprintf(stderr, "Error: '%s' is too large (max 65535 bytes)\n", entry.path.c_str());
            std::exit(1);
        }
        
        // Derive tape filename
        std::string tape_filename = derive_tape_filename(entry.path);
        
        // Write tape file
        write_tape_file(output, file_data, tape_filename, entry.load_address, entry.exec_address);
    }
    
    write_file_bytes(args.output_path, output);
}

// ============================================================================
// Main
// ============================================================================

#ifndef K7TOOL_TESTING
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::printf("Usage: k7tool <file.k7>\n");
        std::printf("       k7tool -o <output.k7> <file:load:exec> [...]\n");
        return 0;
    }

    // Check for create mode (-o flag)
    bool has_o = false;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-o") == 0) {
            has_o = true;
            break;
        }
    }

    if (has_o) {
        auto args = parse_create_args(argc, argv);
        if (!args) return 1;
        create_k7(*args);
    } else {
        inspect(argv[1]);
    }

    return 0;
}
#endif
