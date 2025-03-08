/**
 * Helper file to define ring buffers used in datastore.
 *
 * Created: ?
 *
 * Author: Siddharta Laloux
 */

#include "ring_buffer.h"

#include <string.h>

// HELPER FUNCTIONS

// API

status_t read(ring_buffer_t *p_buffer, void *read_dest) {
    // if we have at least one element
    if (p_buffer->elements) {
        // copy top element into dest
        memcpy(read_dest, p_buffer->head, p_buffer->read_size);
        return SUCCESS;
    } else {
        return ERROR_NO_DATA;
    }
}

/**
 * \name status_t read_n(ring_buffer_t *p_buffer, int num_read, void *read_dest)
 *
 * \param p_buffer: a pointer to a ring buffer, the buffer to read from
 * \param num_read: an int, the number of values to read
 * \param read_dest: a pointer to the destination to read to
 *
 * \return a status_t, whether the read was succesful
 */
status_t read_n(ring_buffer_t *p_buffer, int num_read, void *read_dest) {
    // if we have enough elements stored
    if (num_read >= p_buffer->elements) {
        // copy requisite elements into dest
        size_t total_copy_size = num_read * (p_buffer->read_size);
        size_t copy_one = p_buffer->buffer - p_buffer->head;

        if (copy_one >= total_copy_size) {
            // Should be safe
            memcpy(read_dest, p_buffer->head, total_copy_size);
            return SUCCESS;
        } else if (copy_one < total_copy_size) {
            memcpy(read_dest, p_buffer->head, copy_one);

            // TODO: check pointer arithmetic: probably works in little-endian, but will
            // have to be tested.
            size_t copy_two = total_copy_size - copy_one;
            memccpy(read_dest + copy_one, (p_buffer->buffer + p_buffer->total_buffer_size), copy_two);
            return SUCCESS;
        }
    } else {
        return ERROR_NOT_ENOUGH_DATA;
    }
}

status_t write(void *value) {
    return SUCCESS;
}

status_t init_buffer(void *addr, size_t read_size, int num_to_store) {
    ring_buffer_t *p_rb_header = (ring_buffer_t *)addr;
    void *buffer = p_rb_header + sizeof(ring_buffer_t);
    size_t buffer_size = num_to_store * read_size;

    p_rb_header->buffer = buffer;
    p_rb_header->head = buffer;
    p_rb_header->total_buffer_size = buffer_size;
    p_rb_header->read_size = read_size;
    p_rb_header->elements = 0;

    return SUCCESS;
}