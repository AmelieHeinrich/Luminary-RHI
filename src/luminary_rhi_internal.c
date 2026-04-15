#include "luminary_rhi_internal.h"

#include <stdlib.h>

void lrhi_freelist_init(LRHIFreeList* freelist, uint64_t max_slots)
{
    freelist->max_slots = max_slots;
    freelist->bitmap_size = (max_slots + 63) / 64;
    freelist->bitmap = (uint64_t*)calloc(freelist->bitmap_size, sizeof(uint64_t));
    freelist->free_list = (uint32_t*)malloc(max_slots * sizeof(uint32_t));
    for (uint64_t i = 0; i < max_slots; i++) {
        freelist->free_list[i] = (uint32_t)i;
    }
}

void lrhi_freelist_destroy(LRHIFreeList* freelist)
{
    free(freelist->bitmap);
    free(freelist->free_list);
}

uint32_t lrhi_freelist_allocate(LRHIFreeList* freelist)
{
    for (uint64_t i = 0; i < freelist->bitmap_size; i++) {
        if (freelist->bitmap[i] != UINT64_MAX) {
            for (uint64_t bit = 0; bit < 64; bit++) {
                uint64_t bit_index = i * 64 + bit;
                if (bit_index >= freelist->max_slots) {
                    break;
                }
                if ((freelist->bitmap[i] & (1ULL << bit)) == 0) {
                    lrhi_freelist_set_bit(freelist, (uint32_t)bit_index);
                    return freelist->free_list[bit_index];
                }
            }
        }
    }
    return UINT32_MAX;
}

void lrhi_freelist_free(LRHIFreeList* freelist, uint32_t index)
{
    if (lrhi_freelist_is_bit_set(freelist, index)) {
        lrhi_freelist_clear_bit(freelist, index);
        freelist->free_list[index] = index;
    }
}

void lrhi_freelist_set_bit(LRHIFreeList* freelist, uint32_t index)
{
    if (index >= freelist->max_slots) {
        return;
    }
    uint64_t bit_index = index / 64;
    uint64_t bit_offset = index % 64;
    freelist->bitmap[bit_index] |= (1ULL << bit_offset);
}

void lrhi_freelist_clear_bit(LRHIFreeList* freelist, uint32_t index)
{
    if (index >= freelist->max_slots) {
        return;
    }
    uint64_t bit_index = index / 64;
    uint64_t bit_offset = index % 64;
    freelist->bitmap[bit_index] &= ~(1ULL << bit_offset);
}

uint8_t lrhi_freelist_is_bit_set(LRHIFreeList* freelist, uint32_t index)
{
    if (index >= freelist->max_slots) {
        return 0;
    }
    uint64_t bit_index = index / 64;
    uint64_t bit_offset = index % 64;
    return (freelist->bitmap[bit_index] & (1ULL << bit_offset)) != 0;
}
