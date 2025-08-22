#include "ccrp.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("Usage: %s <filename.crp>\n", argv[0]);
        return 1;
    }
    
    // Read the file
    FILE *f = fopen(argv[1], "r");
    if (!f) {
        printf("Error: Could not open file %s\n", argv[1]);
        return 1;
    }
    
    // Get file size
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    // Allocate buffer and read file
    char *code = malloc(file_size + 1);
    if (!code) {
        printf("Error: Memory allocation failed\n");
        fclose(f);
        return 1;
    }
    
    size_t bytes_read = fread(code, 1, file_size, f);
    code[bytes_read] = '\0';
    fclose(f);
    
    // Interpret the code
    interpret(code);
    
    free(code);
    return 0;
} 