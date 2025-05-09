#ifndef BUDDY_H
#define BUDDY_H

#include <stdbool.h>

#define TOTAL_MEMORY 1024
#define MAX_BLOCK_SIZE 256
#define MIN_BLOCK_SIZE 2  // Smallest block size we'll allocate

typedef struct Block {
    int size;
    int start;
    bool is_free;
    struct Block *next;
    struct Block *buddy;  // Pointer to buddy block
} Block;

// Function prototypes
void init_buddy_system();
void* allocate_memory(int size);
void free_memory(void* ptr);
void print_memory_status();
void cleanup_buddy_system();

#endif 