/**
 * Helper file to define ring buffers used in datastore.
 *
 * Created: Some day in February 2025
 *
 * Author: Siddharta Laloux, Aanya K. Agrawal
 */

#include "ring_buffer.h"

// HELPER FUNCTIONS

// API

/**
 * \fn read
 *
 * \brief read the most recent value from the specified ring buffer
 *
 * \param p_buffer: a pointer to a ring buffer, the buffer to read from
 * \param read_dest: a pointer to the destination to read to
 *
 * \return a status_t, whether the read was succesful
 */
status_t read(ring_buffer_t *p_buffer, sensor_reading_t *read_dest) {
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
 * \fn read_n
 *
 * \brief reads the n most recent values from *p_buffer
 * 
 * \param p_buffer: a pointer to a ring buffer, the buffer to read from
 * \param num_read: an int, the number of values to read
 * \param read_dest: a pointer to the destination to read to
 *
 * \return a status_t, whether the read was succesful
 */
status_t read_n(ring_buffer_t *p_buffer, int num_read, sensor_reading_t *read_dest) {
    // if we have enough elements stored
    if (num_read >= p_buffer->elements) {
        // copy requisite elements into dest
        size_t total_copy_size = num_read * (p_buffer->read_size);
        size_t copy_one = (uint8_t *)p_buffer->head - (uint8_t *)p_buffer->buffer; 

            if (copy_one >= total_copy_size) {
            // Should be safe
            memcpy(read_dest, p_buffer->head, total_copy_size);
            return SUCCESS;
        }
        else if (copy_one < total_copy_size) {
            memcpy(read_dest, p_buffer->head, copy_one);

            // TODO: check pointer arithmetic: probably works in little-endian, but will
            // have to be tested.
            size_t copy_two = total_copy_size - copy_one;
            memcpy(read_dest + copy_one, ((uint8_t *)p_buffer->buffer + p_buffer->total_buffer_size), copy_two);
            return SUCCESS;
        }
    } else {
        return ERROR_NOT_ENOUGH_DATA;
    }
}

/**
 * \fn write
 *
 * \brief write a single value to *p_buffer
 *
 * \param p_buffer: a pointer to a ring buffer, the buffer to read from
 * \param value: a pointer to the new value to be read
 * 
 * \warning not memory-safe! Value is not mutex-protected, so cannot be 
 *      read from
 */
status_t write(ring_buffer_t *p_buffer, sensor_reading_t *value) {
    
    assert_equal(
        sizeof(*value),
        p_buffer->read_size, 
        "ring_buffer: write value not of correct size"
    ); 
    
    // find value write location; head pointer casted to char for pointer 
    // incrementing to be well-defined.
    sensor_reading_t *p_write_location = (sensor_reading_t *)((uint8_t *)p_buffer->head + p_buffer->read_size); 

    // make sure it doesn't overflow allocated buffer size
    if p_write_location == (sensor_reading_t *)((uint8_t *)p_buffer->buffer + p_buffer->total_buffer_size) {
        p_write_location = p_buffer->buffer; 
    }

    // copy from source to dest. 
    memcpy(p_write_location, value, p_buffer->read_size); // there was a c here let it be known

    return SUCCESS;
}

/**
 * \fn init_buffer
 * 
 * \param addr ring_buffer_t *, a pointer to the address where the ring buffer 
 *             should be initialised
 * \param read_size size_t, the size of each reading
 * \param num_to_store int, the number of reading to store
 * 
 * \return a status_t, whether the ring buffer was initialised successfully. 
 */
status_t init_buffer(ring_buffer_t *addr, size_t read_size, int num_to_store) {
    ring_buffer_t *p_rb_header = (ring_buffer_t *)addr;
    void *buffer = p_rb_header + sizeof(ring_buffer_t);
    size_t buffer_size = num_to_store * (read_size + sizeof(TickType_t));

    p_rb_header->buffer = buffer;
    p_rb_header->head = buffer;
    p_rb_header->total_buffer_size = buffer_size;
    p_rb_header->read_size = read_size;
    p_rb_header->elements = 0;

    return SUCCESS;
}

/**
 * \fn init_buffer_consecutive
 *
 * \brief utility function for `init_data_buffer()`. Initialises the specified
 *        buffer and returns the address of the next consecutive buffer
 *
 * \param addr ring_buffer_t *, a pointer to the address where the ring buffer
 *             should be initialised
 * \param read_size size_t, the size of each reading
 * \param num_to_store int, the number of reading to store
 *
 * \return ring_buffer_t *, a pointer to the buffer immediately following the 
 *         initialised buffer
 */
ring_buffer_t *init_buffer_consecutive(ring_buffer_t *addr, size_t read_size, int num_to_store) {
    
    ring_buffer_t *next_ring_buffer_start = addr; 

    fatal_on_error(
        init_buffer(addr, read_size, num_to_store), 
        "Creating ring buffer failed"
    );

    next_ring_buffer_start += sizeof(ring_buffer_t) + num_to_store * (read_size + sizeof(TickType_t));

    return next_ring_buffer_start;
}