#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include "globals.h"

typedef struct ring_buffer {
    void *head; // where to insert or read from
    // void *tail;               // end of the buffer
    void *buffer; // beginning of the buffer
    int elements;
    size_t total_buffer_size; // total size of buffer in bytes
    size_t read_size;         // how many bytes to read per read?
} ring_buffer_t;

status_t read(ring_buffer_t *p_buffer, void *read_dest);
status_t read_n(ring_buffer_t *p_buffer, void *read_dest, int num_read);
status_t write(void *value);
status_t init_buffer(void *addr, size_t read_size, int num_to_store);

#endif // RING_BUFFER_H
