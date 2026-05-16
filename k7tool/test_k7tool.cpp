#ifndef K7TOOL_TESTING
#define K7TOOL_TESTING
#endif
#include "k7tool.cpp"
#include "catch2/catch_amalgamated.hpp"

// ============================================================================
// 3.1 Checksum Tests
// ============================================================================

TEST_CASE("verify_checksum: good checksum", "[checksum]") {
    // "FILLSCR .BIN\x02\x00\x00" header data
    std::vector<uint8_t> data = {
        0x46, 0x49, 0x4C, 0x4C, 0x53, 0x43, 0x52, 0x20,
        0x42, 0x49, 0x4E, 0x02, 0x00, 0x00
    };
    // Compute expected: sum of data bytes
    uint8_t sum = 0;
    for (uint8_t b : data) sum += b;
    uint8_t checksum = static_cast<uint8_t>(-sum);
    REQUIRE(verify_checksum(data, checksum));
}

TEST_CASE("verify_checksum: bad checksum", "[checksum]") {
    std::vector<uint8_t> data = {
        0x46, 0x49, 0x4C, 0x4C, 0x53, 0x43, 0x52, 0x20,
        0x42, 0x49, 0x4E, 0x02, 0x00, 0x00
    };
    REQUIRE_FALSE(verify_checksum(data, 0x00));
}

TEST_CASE("verify_checksum: empty data with checksum 0", "[checksum]") {
    std::vector<uint8_t> data = {};
    REQUIRE(verify_checksum(data, 0x00));
}

// ============================================================================
// 3.2 Header Decoding Tests
// ============================================================================

TEST_CASE("decode_header: standard binary header", "[header]") {
    std::vector<uint8_t> data = {
        0x46, 0x49, 0x4C, 0x4C, 0x53, 0x43, 0x52, 0x20,  // "FILLSCR "
        0x42, 0x49, 0x4E,                                  // "BIN"
        0x02, 0x00, 0x00                                   // binary, binary mode, aux
    };
    auto h = decode_header(data);
    REQUIRE(h.filename == "FILLSCR ");
    REQUIRE(h.extension == "BIN");
    REQUIRE(h.file_type == FILE_TYPE_BINARY);
    REQUIRE(h.data_mode == DATA_MODE_BINARY);
    REQUIRE(h.auxiliary == 0x00);
}

TEST_CASE("decode_header: BASIC header", "[header]") {
    std::vector<uint8_t> data = {
        0x50, 0x52, 0x4F, 0x47, 0x20, 0x20, 0x20, 0x20,  // "PROG    "
        0x42, 0x41, 0x53,                                  // "BAS"
        0x00, 0xFF, 0x00                                   // BASIC, ASCII mode, aux
    };
    auto h = decode_header(data);
    REQUIRE(h.filename == "PROG    ");
    REQUIRE(h.extension == "BAS");
    REQUIRE(h.file_type == FILE_TYPE_BASIC);
    REQUIRE(h.data_mode == DATA_MODE_ASCII);
}

TEST_CASE("decode_header: short header (malformed)", "[header]") {
    std::vector<uint8_t> data = {0x46, 0x49, 0x4C, 0x4C};  // "FILL"
    auto h = decode_header(data);
    REQUIRE(h.filename == "FILL");
    REQUIRE(h.extension.empty());
    REQUIRE(h.file_type == 0x00);
}

// ============================================================================
// 3.3 Binary Stream Decoding Tests
// ============================================================================

TEST_CASE("decode_binary_stream: single segment", "[binary_stream]") {
    // Build a TapeFile with one data block containing one segment + end marker
    TapeFile tf;
    tf.file_number = 1;
    tf.header.file_type = FILE_TYPE_BINARY;

    Block data_block;
    data_block.type = BLOCK_TYPE_DATA;
    // segment: marker=0x00, size=13 (0x000D), load=$6000
    // 13 dummy bytes
    // end: marker=0xFF, reserved=0x00 0x00, exec=$6000
    data_block.data = {
        0x00, 0x00, 0x0D, 0x60, 0x00,  // segment header
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0A, 0x0B, 0x0C, 0x0D,  // 13 data bytes
        0xFF, 0x00, 0x00, 0x60, 0x00    // end marker
    };
    data_block.checksum = 0;
    data_block.checksum_ok = true;
    data_block.file_offset = 0;

    tf.blocks.push_back(data_block);

    decode_binary_stream(tf);

    REQUIRE(tf.segments.size() == 1);
    REQUIRE(tf.segments[0].load_address == 0x6000);
    REQUIRE(tf.segments[0].size == 13);
    REQUIRE(tf.end_of_stream.has_value());
    REQUIRE(tf.end_of_stream->exec_address == 0x6000);
}

TEST_CASE("decode_binary_stream: multi-segment", "[binary_stream]") {
    TapeFile tf;
    tf.file_number = 1;
    tf.header.file_type = FILE_TYPE_BINARY;

    Block data_block;
    data_block.type = BLOCK_TYPE_DATA;
    data_block.data = {
        0x00, 0x00, 0x04, 0x20, 0x00,  // segment 1: size=4, load=$2000
        0xAA, 0xBB, 0xCC, 0xDD,        // 4 data bytes
        0x00, 0x00, 0x03, 0x40, 0x00,  // segment 2: size=3, load=$4000
        0x11, 0x22, 0x33,              // 3 data bytes
        0xFF, 0x00, 0x00, 0x60, 0x00   // end: exec=$6000
    };
    data_block.checksum = 0;
    data_block.checksum_ok = true;
    data_block.file_offset = 0;

    tf.blocks.push_back(data_block);

    decode_binary_stream(tf);

    REQUIRE(tf.segments.size() == 2);
    REQUIRE(tf.segments[0].load_address == 0x2000);
    REQUIRE(tf.segments[0].size == 4);
    REQUIRE(tf.segments[1].load_address == 0x4000);
    REQUIRE(tf.segments[1].size == 3);
    REQUIRE(tf.end_of_stream.has_value());
    REQUIRE(tf.end_of_stream->exec_address == 0x6000);
}

TEST_CASE("decode_binary_stream: split across blocks", "[binary_stream]") {
    TapeFile tf;
    tf.file_number = 1;
    tf.header.file_type = FILE_TYPE_BINARY;

    // Block A: partial segment header (4 bytes — missing addr_lo)
    Block block_a;
    block_a.type = BLOCK_TYPE_DATA;
    block_a.data = {0x00, 0x01, 0x00, 0x60};  // marker, size_hi=1, size_lo=0, addr_hi=0x60
    block_a.checksum = 0;
    block_a.checksum_ok = true;
    block_a.file_offset = 0;

    // Block B: rest of header (addr_lo) + 256 data bytes + end marker
    Block block_b;
    block_b.type = BLOCK_TYPE_DATA;
    block_b.data.push_back(0x00);  // addr_lo = 0x00 → load=$6000
    // 256 bytes of payload
    for (int i = 0; i < 256; i++) block_b.data.push_back(static_cast<uint8_t>(i));
    // end marker
    block_b.data.push_back(0xFF);
    block_b.data.push_back(0x00);
    block_b.data.push_back(0x00);
    block_b.data.push_back(0x60);
    block_b.data.push_back(0x00);
    block_b.checksum = 0;
    block_b.checksum_ok = true;
    block_b.file_offset = 0;

    tf.blocks.push_back(block_a);
    tf.blocks.push_back(block_b);

    decode_binary_stream(tf);

    REQUIRE(tf.segments.size() == 1);
    REQUIRE(tf.segments[0].load_address == 0x6000);
    REQUIRE(tf.segments[0].size == 256);
    REQUIRE(tf.end_of_stream.has_value());
    REQUIRE(tf.end_of_stream->exec_address == 0x6000);
}

// ============================================================================
// 3.4 Block Parser Unit Tests
// ============================================================================

TEST_CASE("read_block: normal data block", "[block_parser]") {
    // type=0x01, length=5 (data=3 bytes + checksum)
    uint8_t sum = static_cast<uint8_t>(0xAA + 0xBB + 0xCC);
    uint8_t checksum = static_cast<uint8_t>(-sum);

    std::vector<uint8_t> raw = {0x01, 0x05, 0xAA, 0xBB, 0xCC, checksum};
    auto result = read_block(raw, 0);

    REQUIRE(result.has_value());
    REQUIRE(result->block.type == BLOCK_TYPE_DATA);
    REQUIRE(result->block.data.size() == 3);
    REQUIRE(result->block.data[0] == 0xAA);
    REQUIRE(result->block.data[1] == 0xBB);
    REQUIRE(result->block.data[2] == 0xCC);
    REQUIRE(result->block.checksum_ok);
}

TEST_CASE("read_block: full block (length=0 means 256)", "[block_parser]") {
    // type=0x01, length=0x00 (effective=256, data=254 bytes + checksum)
    std::vector<uint8_t> raw;
    raw.push_back(0x01);  // type
    raw.push_back(0x00);  // length byte = 0 means effective 256
    uint8_t sum = 0;
    for (int i = 0; i < 254; i++) {
        raw.push_back(static_cast<uint8_t>(i & 0xFF));
        sum += static_cast<uint8_t>(i & 0xFF);
    }
    raw.push_back(static_cast<uint8_t>(-sum));  // checksum

    auto result = read_block(raw, 0);

    REQUIRE(result.has_value());
    REQUIRE(result->block.type == BLOCK_TYPE_DATA);
    REQUIRE(result->block.data.size() == 254);
    REQUIRE(result->block.checksum_ok);
}

TEST_CASE("read_block: EOF block", "[block_parser]") {
    // type=0xFF, length=0x02, checksum=0x00
    std::vector<uint8_t> raw = {0xFF, 0x02, 0x00};
    auto result = read_block(raw, 0);

    REQUIRE(result.has_value());
    REQUIRE(result->block.type == BLOCK_TYPE_EOF);
    REQUIRE(result->block.data.empty());
    REQUIRE(result->block.checksum_ok);
}

// ============================================================================
// 3.5 Leader/Sync Finding Tests
// ============================================================================

TEST_CASE("find_next_block_start: leader at start", "[leader]") {
    std::vector<uint8_t> tape;
    // "DCMOTO"
    for (uint8_t c : LEADER_SIG) tape.push_back(c);
    // 10x 0x01
    for (int i = 0; i < PILOT_LEN; i++) tape.push_back(PILOT_BYTE);
    // Sync
    tape.push_back(SYNC_WORD[0]);
    tape.push_back(SYNC_WORD[1]);
    // Dummy block data
    tape.push_back(0x01);
    tape.push_back(0x03);
    tape.push_back(0xAA);
    tape.push_back(0x56);  // checksum for 0xAA

    auto result = find_next_block_start(tape, 0);
    REQUIRE(result.has_value());
    REQUIRE(*result == 18);  // 6 + 10 + 2
}

TEST_CASE("find_next_block_start: leader with garbage prefix", "[leader]") {
    std::vector<uint8_t> tape = {0x00, 0x00, 0xFF, 0xFF};  // 4 bytes garbage
    for (uint8_t c : LEADER_SIG) tape.push_back(c);
    for (int i = 0; i < PILOT_LEN; i++) tape.push_back(PILOT_BYTE);
    tape.push_back(SYNC_WORD[0]);
    tape.push_back(SYNC_WORD[1]);

    auto result = find_next_block_start(tape, 0);
    REQUIRE(result.has_value());
    REQUIRE(*result == 22);  // 4 + 6 + 10 + 2
}

TEST_CASE("find_next_block_start: no leader found", "[leader]") {
    std::vector<uint8_t> tape = {0x00, 0x00, 0x00, 0x00};
    auto result = find_next_block_start(tape, 0);
    REQUIRE_FALSE(result.has_value());
}

// ============================================================================
// 3.6 Integration: Parse Sample Files
// ============================================================================

TEST_CASE("parse samples without crash", "[integration]") {
    for (auto& entry : fs::directory_iterator("samples")) {
        if (entry.path().extension() == ".k7") {
            INFO("Parsing: " << entry.path().filename().string());
            auto data = read_file_bytes(entry.path().string());
            auto result = parse_tape(data);
            // Must find at least one file
            REQUIRE(result.files.size() >= 1);
            // Each file must have a header block and an EOF block
            for (auto& f : result.files) {
                REQUIRE(f.blocks.size() >= 2);
                REQUIRE(f.blocks.front().type == BLOCK_TYPE_HEADER);
                REQUIRE(f.blocks.back().type == BLOCK_TYPE_EOF);
            }
        }
    }
}

// ============================================================================
// 3.7 Integration: Verify Checksums
// ============================================================================

TEST_CASE("all sample checksums pass", "[integration]") {
    for (auto& entry : fs::directory_iterator("samples")) {
        if (entry.path().extension() == ".k7") {
            INFO("File: " << entry.path().filename().string());
            auto data = read_file_bytes(entry.path().string());
            auto result = parse_tape(data);
            for (auto& f : result.files) {
                for (auto& block : f.blocks) {
                    INFO("  block at offset " << block.file_offset);
                    REQUIRE(block.checksum_ok);
                }
            }
        }
    }
}

// ============================================================================
// 3.8 Integration: Spot-Check Known Values
// ============================================================================

TEST_CASE("fillscr-mo5.k7 addresses", "[integration]") {
    auto data = read_file_bytes("samples/fillscr-mo5.k7");
    auto result = parse_tape(data);
    REQUIRE(result.files.size() >= 1);

    auto& file = result.files[0];
    REQUIRE(file.header.filename == "FILLSCR ");
    REQUIRE(file.header.file_type == FILE_TYPE_BINARY);
    REQUIRE(file.segments.size() >= 1);
    REQUIRE(file.segments[0].load_address == 0x6000);
    REQUIRE(file.end_of_stream.has_value());
    REQUIRE(file.end_of_stream->exec_address == 0x6000);
}

// ============================================================================
// 5.1 Filename Derivation Tests
// ============================================================================

TEST_CASE("derive_tape_filename: simple name", "[filename]") {
    REQUIRE(derive_tape_filename("viewer.bin") == "VIEWER  ");
}

TEST_CASE("derive_tape_filename: long name truncation", "[filename]") {
    REQUIRE(derive_tape_filename("longfilename.bin") == "LONGFILE");
}

TEST_CASE("derive_tape_filename: short name padding", "[filename]") {
    REQUIRE(derive_tape_filename("a.bin") == "A       ");
}

TEST_CASE("derive_tape_filename: path stripping", "[filename]") {
    REQUIRE(derive_tape_filename("/home/user/project/viewer.bin") == "VIEWER  ");
}

TEST_CASE("derive_tape_filename: mixed case", "[filename]") {
    REQUIRE(derive_tape_filename("MyImage.mo5z") == "MYIMAGE ");
}

TEST_CASE("derive_tape_filename: dotted stem", "[filename]") {
    REQUIRE(derive_tape_filename("image1.mo5z") == "IMAGE1  ");
}

// ============================================================================
// 5.2 Hex Address Parsing Tests
// ============================================================================

TEST_CASE("parse_hex_address: valid addresses", "[hex]") {
    REQUIRE(parse_hex_address("0x6000") == 0x6000);
    REQUIRE(parse_hex_address("0x0000") == 0x0000);
    REQUIRE(parse_hex_address("0xFFFF") == 0xFFFF);
    REQUIRE(parse_hex_address("0X2800") == 0x2800);
}

TEST_CASE("parse_hex_address: invalid addresses", "[hex]") {
    REQUIRE_FALSE(parse_hex_address("6000").has_value());
    REQUIRE_FALSE(parse_hex_address("0x").has_value());
    REQUIRE_FALSE(parse_hex_address("0xGGGG").has_value());
    REQUIRE_FALSE(parse_hex_address("0x10000").has_value());
    REQUIRE_FALSE(parse_hex_address("").has_value());
}

// ============================================================================
// 5.3 Block Writing Tests
// ============================================================================

TEST_CASE("write_block: header block roundtrip", "[write_block]") {
    std::vector<uint8_t> data = {
        0x46, 0x49, 0x4C, 0x4C, 0x53, 0x43, 0x52, 0x20,
        0x42, 0x49, 0x4E, 0x02, 0x00, 0x00
    };
    std::vector<uint8_t> out;
    write_block(out, BLOCK_TYPE_HEADER, data);

    // Parse it back
    auto parsed = read_block(out, 0);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->block.type == BLOCK_TYPE_HEADER);
    REQUIRE(parsed->block.data == data);
    REQUIRE(parsed->block.checksum_ok);
}

TEST_CASE("write_block: full 254-byte block", "[write_block]") {
    std::vector<uint8_t> data(254, 0xAB);
    std::vector<uint8_t> out;
    write_block(out, BLOCK_TYPE_DATA, data);

    REQUIRE(out[1] == 0x00);  // length byte wraps to 0 for 256

    auto parsed = read_block(out, 0);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->block.data.size() == 254);
    REQUIRE(parsed->block.checksum_ok);
}

TEST_CASE("write_block: EOF block", "[write_block]") {
    std::vector<uint8_t> out;
    std::span<const uint8_t> empty;
    write_block(out, BLOCK_TYPE_EOF, empty);

    REQUIRE(out.size() == 3);
    REQUIRE(out[0] == 0xFF);
    REQUIRE(out[1] == 0x02);
    REQUIRE(out[2] == 0x00);

    auto parsed = read_block(out, 0);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->block.type == BLOCK_TYPE_EOF);
    REQUIRE(parsed->block.data.empty());
    REQUIRE(parsed->block.checksum_ok);
}

// ============================================================================
// 5.4 Binary Stream Building Tests
// ============================================================================

TEST_CASE("build_binary_stream: small file", "[stream]") {
    std::vector<uint8_t> code = {0x8E, 0x00, 0x00, 0x86, 0xFF, 0xA7, 0x80,
                                  0x8C, 0x20, 0x00, 0x26, 0xF9, 0x39};
    auto stream = build_binary_stream(code, 0x6000, 0x6000);

    REQUIRE(stream.size() == 23);
    // Segment header
    REQUIRE(stream[0] == 0x00);
    REQUIRE(stream[1] == 0x00);
    REQUIRE(stream[2] == 0x0D);  // size=13
    REQUIRE(stream[3] == 0x60);
    REQUIRE(stream[4] == 0x00);  // load=$6000
    // End marker
    REQUIRE(stream[18] == 0xFF);
    REQUIRE(stream[19] == 0x00);
    REQUIRE(stream[20] == 0x00);
    REQUIRE(stream[21] == 0x60);
    REQUIRE(stream[22] == 0x00);  // exec=$6000
}

TEST_CASE("build_binary_stream: empty file", "[stream]") {
    std::vector<uint8_t> empty_data;
    auto stream = build_binary_stream(empty_data, 0x2000, 0x2000);

    REQUIRE(stream.size() == 10);
    REQUIRE(stream[0] == 0x00);
    REQUIRE(stream[1] == 0x00);
    REQUIRE(stream[2] == 0x00);  // size=0
    REQUIRE(stream[3] == 0x20);
    REQUIRE(stream[4] == 0x00);
    REQUIRE(stream[5] == 0xFF);
    REQUIRE(stream[6] == 0x00);
    REQUIRE(stream[7] == 0x00);
    REQUIRE(stream[8] == 0x20);
    REQUIRE(stream[9] == 0x00);
}

// ============================================================================
// 5.5 Chunk Splitting Tests
// ============================================================================

TEST_CASE("split_into_chunks: fits in one block", "[chunks]") {
    std::vector<uint8_t> stream(23, 0xAA);
    auto chunks = split_into_chunks(stream);
    REQUIRE(chunks.size() == 1);
    REQUIRE(chunks[0].size() == 23);
}

TEST_CASE("split_into_chunks: exact boundary", "[chunks]") {
    std::vector<uint8_t> stream(254, 0xAA);
    auto chunks = split_into_chunks(stream);
    REQUIRE(chunks.size() == 1);
    REQUIRE(chunks[0].size() == 254);
}

TEST_CASE("split_into_chunks: straddles boundary", "[chunks]") {
    std::vector<uint8_t> stream(255, 0xAA);
    auto chunks = split_into_chunks(stream);
    REQUIRE(chunks.size() == 2);
    REQUIRE(chunks[0].size() == 254);
    REQUIRE(chunks[1].size() == 1);
}

TEST_CASE("split_into_chunks: multiple full blocks", "[chunks]") {
    std::vector<uint8_t> stream(762, 0xAA);
    auto chunks = split_into_chunks(stream);
    REQUIRE(chunks.size() == 3);
    REQUIRE(chunks[0].size() == 254);
    REQUIRE(chunks[1].size() == 254);
    REQUIRE(chunks[2].size() == 254);
}

TEST_CASE("split_into_chunks: multiple blocks + remainder", "[chunks]") {
    std::vector<uint8_t> stream(600, 0xAA);
    auto chunks = split_into_chunks(stream);
    REQUIRE(chunks.size() == 3);
    REQUIRE(chunks[0].size() == 254);
    REQUIRE(chunks[1].size() == 254);
    REQUIRE(chunks[2].size() == 92);
}

// ============================================================================
// 5.6 Round-Trip Tests
// ============================================================================

TEST_CASE("round-trip: single file", "[create][roundtrip]") {
    std::vector<uint8_t> code = {0x8E, 0x00, 0x00, 0x86, 0xFF, 0xA7, 0x80,
                                  0x8C, 0x20, 0x00, 0x26, 0xF9, 0x39};

    std::vector<uint8_t> k7_data;
    write_tape_file(k7_data, code, "FILLSCR ", 0x6000, 0x6000);

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
    for (auto& block : f.blocks) REQUIRE(block.checksum_ok);
}

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

    for (auto& f : result.files)
        for (auto& b : f.blocks)
            REQUIRE(b.checksum_ok);
}

TEST_CASE("round-trip: large file multiple blocks", "[create][roundtrip]") {
    std::vector<uint8_t> big_file(762, 0xCC);

    std::vector<uint8_t> k7_data;
    write_tape_file(k7_data, big_file, "BIGFILE ", 0x3000, 0x3000);

    auto result = parse_tape(k7_data);
    REQUIRE(result.files.size() == 1);
    auto& f = result.files[0];
    REQUIRE(f.segments.size() == 1);
    REQUIRE(f.segments[0].load_address == 0x3000);
    REQUIRE(f.segments[0].size == 762);
    REQUIRE(f.end_of_stream->exec_address == 0x3000);
    for (auto& block : f.blocks) REQUIRE(block.checksum_ok);

    // stream = 762 + 10 = 772 bytes → ceil(772/254) = 4 data blocks
    int data_block_count = 0;
    for (auto& b : f.blocks) if (b.type == BLOCK_TYPE_DATA) data_block_count++;
    REQUIRE(data_block_count == 4);
}

// ============================================================================
// 5.7 Byte-Level Comparison (Golden Test)
// ============================================================================

TEST_CASE("byte-level match with fillscr-mo5.k7", "[create][golden]") {
    std::vector<uint8_t> code = {0x8E, 0x00, 0x00, 0x86, 0xFF, 0xA7, 0x80,
                                  0x8C, 0x20, 0x00, 0x26, 0xF9, 0x39};

    std::vector<uint8_t> generated;
    write_tape_file(generated, code, "FILLSCR ", 0x6000, 0x6000);

    auto sample = read_file_bytes("samples/fillscr-mo5.k7");

    REQUIRE(generated.size() == sample.size());
    REQUIRE(generated == sample);
}

// ============================================================================
// 5.8 Argument Parsing Tests
// ============================================================================

TEST_CASE("parse_create_args: valid single file", "[args]") {
    const char* argv[] = {"k7tool", "-o", "out.k7", "file.bin:0x6000:0x6000"};
    auto args = parse_create_args(4, const_cast<char**>(argv));
    REQUIRE(args.has_value());
    REQUIRE(args->output_path == "out.k7");
    REQUIRE(args->entries.size() == 1);
    REQUIRE(args->entries[0].path == "file.bin");
    REQUIRE(args->entries[0].load_address == 0x6000);
    REQUIRE(args->entries[0].exec_address == 0x6000);
}

TEST_CASE("parse_create_args: multiple files", "[args]") {
    const char* argv[] = {"k7tool", "-o", "out.k7", "a.bin:0x2000:0x2000", "b.bin:0x4000:0x4000"};
    auto args = parse_create_args(5, const_cast<char**>(argv));
    REQUIRE(args.has_value());
    REQUIRE(args->entries.size() == 2);
}

TEST_CASE("parse_create_args: -o at end", "[args]") {
    const char* argv[] = {"k7tool", "a.bin:0x2000:0x2000", "-o", "out.k7"};
    auto args = parse_create_args(4, const_cast<char**>(argv));
    REQUIRE(args.has_value());
    REQUIRE(args->output_path == "out.k7");
    REQUIRE(args->entries.size() == 1);
}

TEST_CASE("parse_create_args: missing -o value", "[args]") {
    const char* argv[] = {"k7tool", "-o"};
    auto args = parse_create_args(2, const_cast<char**>(argv));
    REQUIRE_FALSE(args.has_value());
}

TEST_CASE("parse_create_args: no file entries", "[args]") {
    const char* argv[] = {"k7tool", "-o", "out.k7"};
    auto args = parse_create_args(3, const_cast<char**>(argv));
    REQUIRE_FALSE(args.has_value());
}

TEST_CASE("parse_create_args: bad entry format", "[args]") {
    const char* argv[] = {"k7tool", "-o", "out.k7", "file.bin:0x6000"};
    auto args = parse_create_args(4, const_cast<char**>(argv));
    REQUIRE_FALSE(args.has_value());
}
