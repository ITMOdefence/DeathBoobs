#include "fake_backend_internal.h"

static usize live_blocks;
static bool failure_armed;
static usize allocations_before_failure;
static ErrorCode free_failure;

void fake_memory_fail_after(usize successful_allocations) {
    failure_armed = true;
    allocations_before_failure = successful_allocations;
}

void fake_memory_clear_failure(void) {
    failure_armed = false;
    allocations_before_failure = 0;
    free_failure = CORE_OK;
}

void fake_memory_fail_next_free(ErrorCode error) {
    free_failure = error;
}

usize fake_memory_live_blocks(void) {
    return live_blocks;
}

static bool allocation_fails(void) {
    if (!failure_armed) {
        return false;
    }
    if (allocations_before_failure != 0) {
        --allocations_before_failure;
        return false;
    }
    failure_armed = false;
    return true;
}

static bool valid_block(const MemoryBlock *block) {
    return block != NULL && ((block->data == NULL) == (block->size == 0));
}

ErrorCode memory_allocate(usize size, MemoryBlock *out_block) {
    if (out_block == NULL) {
        return CORE_INVALID_ARGUMENT;
    }
    if (size == 0) {
        *out_block = (MemoryBlock){0};
        return CORE_OK;
    }
    if (allocation_fails()) {
        return CORE_OUT_OF_MEMORY;
    }
    u8 *bytes = malloc(size);
    if (bytes == NULL) {
        return CORE_OUT_OF_MEMORY;
    }
    for (usize i = 0; i < size; ++i) {
        bytes[i] = 0;
    }
    ++live_blocks;
    *out_block = (MemoryBlock){bytes, size};
    return CORE_OK;
}

ErrorCode memory_free(MemoryBlock *block) {
    if (!valid_block(block)) {
        return CORE_INVALID_ARGUMENT;
    }
    if (block->data != NULL) {
        if (free_failure != CORE_OK) {
            ErrorCode error = free_failure;
            free_failure = CORE_OK;
            return error;
        }
        free(block->data);
        --live_blocks;
    }
    *block = (MemoryBlock){0};
    return CORE_OK;
}

ErrorCode memory_reallocate(MemoryBlock *block, usize new_size) {
    if (!valid_block(block)) {
        return CORE_INVALID_ARGUMENT;
    }
    if (new_size == 0) {
        return memory_free(block);
    }
    if (new_size == block->size) {
        return CORE_OK;
    }
    MemoryBlock replacement = {0};
    ErrorCode error = memory_allocate(new_size, &replacement);
    if (error != CORE_OK) {
        return error;
    }
    usize copied = block->size < new_size ? block->size : new_size;
    for (usize i = 0; i < copied; ++i) {
        replacement.data[i] = block->data[i];
    }
    error = memory_free(block);
    if (error != CORE_OK) {
        (void)memory_free(&replacement);
        return error;
    }
    *block = replacement;
    return CORE_OK;
}

ErrorCode memory_set(Bytes destination, u8 value) {
    if (destination.data == NULL && destination.size != 0) {
        return CORE_INVALID_ARGUMENT;
    }
    for (usize i = 0; i < destination.size; ++i) {
        destination.data[i] = value;
    }
    return CORE_OK;
}

ErrorCode memory_copy(Bytes destination, Bytes source) {
    if (source.size > destination.size ||
        (source.data == NULL && source.size != 0) ||
        (destination.data == NULL && destination.size != 0)) {
        return CORE_INVALID_ARGUMENT;
    }
    if (source.size != 0) {
        usize left = (usize)destination.data;
        usize right = (usize)source.data;
        if ((left >= right && left - right < source.size) ||
            (right > left && right - left < source.size)) {
            return CORE_INVALID_ARGUMENT;
        }
    }
    for (usize i = 0; i < source.size; ++i) {
        destination.data[i] = source.data[i];
    }
    return CORE_OK;
}

ErrorCode memory_move(Bytes destination, Bytes source) {
    if (source.size > destination.size ||
        (source.data == NULL && source.size != 0) ||
        (destination.data == NULL && destination.size != 0)) {
        return CORE_INVALID_ARGUMENT;
    }
    if ((usize)destination.data > (usize)source.data) {
        for (usize i = source.size; i != 0; --i) {
            destination.data[i - 1] = source.data[i - 1];
        }
    } else {
        for (usize i = 0; i < source.size; ++i) {
            destination.data[i] = source.data[i];
        }
    }
    return CORE_OK;
}
