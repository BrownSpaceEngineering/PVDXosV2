typedef struct ring_buffer {
    void *head;
    void *tail;
    void *buffer;
    size_t total_buffer_size;
    size_t read_size;
};
