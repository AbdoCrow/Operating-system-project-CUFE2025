#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "buddy.h"

static Block* free_blocks[10];  // Array of free block lists for different sizes
                                //think of it as a linked list for each size (buckets)
static char memory[TOTAL_MEMORY];  // The actual memory space
static Block* allocated_blocks = NULL;  // List of all allocated blocks
static int total_allocated = 0;

// Initialize the buddy system
void init_buddy_system() {
    // Initialize all free block lists to NULL
    for (int i = 0; i < 10; i++) {
        free_blocks[i] = NULL;
    }
    
    // Create initial block of total memory
    Block* initial_block = (Block*)malloc(sizeof(Block));
    if (initial_block == NULL) {
        perror("Error: Failed to allocate memory for initial block\n");
        exit(1);
    }
    initial_block->size = TOTAL_MEMORY;
    initial_block->start = 0;
    initial_block->is_free = true;
    initial_block->next = NULL;
    initial_block->buddy = NULL;
    
    // Add to appropriate free list (log2(1024) = 10)
    free_blocks[9] = initial_block;
}

// Find the appropriate block size for allocation
 int get_block_size(int size) {
    int block_size = MIN_BLOCK_SIZE;
    while (block_size < size && block_size < MAX_BLOCK_SIZE) {
        block_size *= 2;
    }
    return block_size;
}

// Find the index in free_blocks array for a given size
static int get_block_index(int size) {
    int index = 0;
    while ((1 << (index + 1)) < size) { //if you don't understand the << operator, think of it as a multiplication by 2
                                        //we used something like this in Logic project ALU
        index++;
    }
    return index;
}

// Split a block into two buddies
static void split_block(Block* block) {
    int new_size = block->size / 2;
    int new_start = block->start + new_size;
    
    // Create new buddy block
    Block* buddy = (Block*)malloc(sizeof(Block));
    
    buddy->size = new_size;
    buddy->start = new_start;
    buddy->is_free = true;
    buddy->next = NULL;
    
    // Update original block
    block->size = new_size;
    
    // Link buddies
    block->buddy = buddy;
    buddy->buddy = block;
    
    // Add buddy to appropriate free list
    int index = get_block_index(new_size);
    buddy->next = free_blocks[index];
    free_blocks[index] = buddy;
}

// merge buddy blocks if both are free
static void mergeFreeBlocks(Block* block) {
    if (!block || !block->buddy) return;
    
    Block* buddy = block->buddy;
    if (!buddy->is_free) return;
    
    // Determine which block is the left one
    Block* left = (block->start < buddy->start) ? block : buddy;
    Block* right = (block->start < buddy->start) ? buddy : block;
    
    // Remove both blocks from their current free lists
    int index = get_block_index(left->size);
    Block* current = free_blocks[index];
    Block* prev = NULL;
    
    // Remove left block from free list
    while (current != NULL) {
        if (current == left) {
            if (prev) {
                prev->next = current->next;
            } else {
                free_blocks[index] = current->next;
            }
            break;
        }
        prev = current;
        current = current->next;
    }
    
    // Remove right block from free list
    //I added a FIX here to remove the right block from the free list 
    //I was getting an error because the right block was not being removed from the free list thought free will remove it
    current = free_blocks[index];
    prev = NULL;
    while (current != NULL) {
        if (current == right) {
            if (prev) {
                prev->next = current->next;
            } else {
                free_blocks[index] = current->next;
            }
            break;
        }
        prev = current;
        current = current->next;
    }
    
    // Create new merged block
    left->size *= 2;
    left->buddy = NULL;
    
    // Free the right block's metadata
    free(right);
    right->size = 0;
    
    // Add merged block to appropriate free list
    index = get_block_index(left->size);
    left->next = free_blocks[index];
    free_blocks[index] = left;
    printf("Merged blocks: %d + %d = %d bytes at offset %d\n", 
           left->size/2, left->size/2, left->size, left->start);
    
    // Try to merge again if possible
    mergeFreeBlocks(left);
}

// Allocate memory of given size
void* allocate_memory(int size) {
    if (size > MAX_BLOCK_SIZE || size <= 0 || size > TOTAL_MEMORY) {
        printf("Error: Requested size %d exceeds available memory %d\n", size, TOTAL_MEMORY);
        return NULL;
    }
    
    int block_size = get_block_size(size);
    if (block_size > TOTAL_MEMORY) {
        printf("Error: Block size %d exceeds total memory %d\n", block_size, TOTAL_MEMORY);
        return NULL;
    }
    
    if (total_allocated + block_size > TOTAL_MEMORY) {
        printf("Error: Not enough memory available. Requested: %d, Available: %d\n", 
               block_size, TOTAL_MEMORY - total_allocated);
        return NULL;
    }
    
    int index = get_block_index(block_size);
    
    // Find a suitable block
    Block* block = NULL;
    Block* prev = NULL;
    Block* current = free_blocks[index];
    
    while (current != NULL) {
        if (current->is_free && current->size >= block_size) {
            block = current;
            if (prev) {
                prev->next = current->next;
            } else {
                free_blocks[index] = current->next;
            }
            break;
        }
        prev = current;
        current = current->next;
    }
    
    // If no suitable block found, try to split larger blocks
    if (!block) {
        for (int i = index + 1; i < 10; i++) {
            if (free_blocks[i] != NULL) {
                block = free_blocks[i];
                free_blocks[i] = block->next;
                
                // Split the block until we get the right size
                while (block->size > block_size) {
                    split_block(block);
                }
                break;
            }
        }
    }
    
    if (block) {
        // Verify the block's start address is within bounds
        if (block->start < 0 || block->start + block->size > TOTAL_MEMORY) {
            printf("Error: Block address out of bounds (start: %d, size: %d)\n", 
                   block->start, block->size);
            return NULL;
        }
        
        block->is_free = false;
        total_allocated += block->size;
        
        // Add to allocated blocks list
        block->next = allocated_blocks;
        allocated_blocks = block;
        
        void* ptr = &memory[block->start];
        printf("Allocated %d bytes at offset %d -> %p (Total allocated: %d/%d)\n", 
               block->size, block->start, ptr, total_allocated, TOTAL_MEMORY);
        return ptr;
    }
    
    return NULL;  // No memory available
}

// Free allocated memory
void free_memory(void* ptr) {
    if (!ptr) {
        printf("Error: Attempting to free NULL pointer\n");
        return;
    }
    
    // Validate pointer is within memory bounds
    if (ptr < (void*)memory || ptr >= (void*)(memory + TOTAL_MEMORY)) {
        printf("Error: Invalid memory pointer %p (valid range: %p - %p)\n", 
               ptr, (void*)memory, (void*)(memory + TOTAL_MEMORY));
        return;
    }
    
    // Find the block in allocated blocks list
    int offset = (char*)ptr - memory;
    if (offset < 0 || offset >= TOTAL_MEMORY) {
        printf("Error: Invalid memory offset %d (valid range: 0 - %d)\n", 
               offset, TOTAL_MEMORY - 1);
        return;
    }
    
    Block* block = allocated_blocks;
    Block* prev = NULL;
    
    while (block != NULL) {
        if (block->start == offset) {
            // Remove from allocated blocks list
            if (prev) {
                prev->next = block->next;
            } else {
                allocated_blocks = block->next;
            }
            
            // Mark as free and add to free list
            block->is_free = true;
            total_allocated -= block->size;
            printf("Freed %d bytes at offset %d (Total allocated: %d/%d)\n", 
                   block->size, block->start, total_allocated, TOTAL_MEMORY);
            
            int index = get_block_index(block->size);
            block->next = free_blocks[index];
            free_blocks[index] = block;
            
            // Try to merge with buddy
            mergeFreeBlocks(block);
            return;
        }
        prev = block;
        block = block->next;
    }
    
    printf("Error: Attempting to free unallocated memory at offset %d\n", offset);
}


// Clean up the buddy system
void cleanup_buddy_system() {
    // Free all allocated blocks
    Block* current = allocated_blocks;
    while (current != NULL) {
        Block* next = current->next;
        current->next = NULL;  // Clear next pointer before freeing
        free(current);
        current = next;
    }
    allocated_blocks = NULL;
    
    // Free all free blocks
    for (int i = 0; i < 10; i++) {
        current = free_blocks[i];
        while (current != NULL) {
            Block* next = current->next;
            if (current->size > 0 && current->size <= TOTAL_MEMORY) {  // Validate block size
                current->next = NULL;  // Clear next pointer before freeing
                current->buddy = NULL; // Clear buddy pointer before freeing
                free(current);
            }
            current = next;
        }
        free_blocks[i] = NULL;
    }
    
    // Verify all memory is freed
    if (total_allocated == 0) {
        printf("All memory successfully freed. (%d bytes)\n", TOTAL_MEMORY);
    } else {
        printf("Memory leak detected. %d bytes still allocated.\n", total_allocated);
    }
}

