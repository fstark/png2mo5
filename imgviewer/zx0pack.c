/*
 * zx0pack.c — Standalone ZX0 compressor
 * Usage: zx0pack input output
 * Compresses input file and writes raw ZX0 stream to output.
 */
#include <stdio.h>
#include <stdlib.h>
#include "../mo5z/zx0/zx0.h"

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: zx0pack input output\n");
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    int input_size = (int)ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *input = malloc(input_size);
    if (fread(input, 1, input_size, f) != (size_t)input_size) {
        fprintf(stderr, "Read error\n");
        return 1;
    }
    fclose(f);

    BLOCK *optimal = optimize(input, input_size, 0, 32640);
    int output_size = 0;
    int delta = 0;
    unsigned char *output = compress(optimal, input, input_size, 0, 0, 0, &output_size, &delta);

    f = fopen(argv[2], "wb");
    if (!f) { perror(argv[2]); return 1; }
    if (fwrite(output, 1, output_size, f) != (size_t)output_size) {
        fprintf(stderr, "Write error\n");
        return 1;
    }
    fclose(f);

    fprintf(stderr, "%d -> %d bytes (%.1f%%)\n", input_size, output_size, 100.0 * output_size / input_size);
    free(input);
    free(output);
    return 0;
}
