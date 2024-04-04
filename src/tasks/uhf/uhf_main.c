#include "globals.h"
#include "uhf_hal.h"
#include "uhf_task.h"

struct uhfTaskMemory uhfMem = {0};
StreamBufferHandle_t uhfIncomingMessages = {0};
StreamBufferHandle_t uhfOutgoingMessages = {0};
bool streamsInitialized = 0;

// Memory for the streams
uint8_t uhfIncomingMessagesBuffer[UHF_INCOMING_MESSAGES_STREAM_LENGTH] = {0};
uint8_t uhfOutgoingMessagesBuffer[UHF_OUTGOING_MESSAGES_STREAM_LENGTH] = {0};
StaticStreamBuffer_t uhfIncomingMessagesStruct = {0};
StaticStreamBuffer_t uhfOutgoingMessagesStruct = {0};

// Mutex for the outgoing messages buffer
SemaphoreHandle_t uhfOutgoingMessagesMutex = {0};
StaticSemaphore_t uhfOutgoingMessagesMutexStruct = {0};

// Memory to store data after it's removed from the stream
uint8_t uhf_next_packet[MAX_PKT_LENGTH] = {0};

unsigned char uhf_test_message[] = "Brown UHF Engineering! 🚀🚀🚀🚀🚀";
size_t uhf_test_message_length = sizeof(uhf_test_message);

// Definitions for static functions in this file
static status_t init_stream_buffers();

// Main function
void uhf_main(void *pvParameters) {
    info("UHF task started!\n");
    if (SUCCESS != init_stream_buffers()) {
        fatal("Failed to initialize stream buffers for UHF task!\n");
    }

    // Initialize the UHF hardware
    status_t init_status = uhf_init(433E6);
    if (init_status == SUCCESS) {
        info("UHF module initialized succesfully\n");
    } else {
        warning("Failed to initialize UHF module! [Error: %d]\n", init_status);
    }

    // Constantly poll the UHF module for new messages
    // Constantly check the stream to see if anything needs to be sent
    while (1) {
        // Check for incoming messages by reading from the incoming stream into the next packet buffer
        size_t bytes_read = xStreamBufferReceive(uhfIncomingMessages, uhf_next_packet, MAX_PKT_LENGTH, 0);
        if (bytes_read > 0) {
            debug("Received UHF message of length %d\n", bytes_read);
        }

        vTaskDelay(pdMS_TO_TICKS(UHF_POLLING_DELAY_MS));
    }

    status_t send_status = uhf_send(uhf_test_message, uhf_test_message_length);
    if (send_status == SUCCESS) {
        info("UHF message sent succesfully\n");
    } else {
        warning("Failed to send UHF message! [Error: %d]\n", send_status);
    }

    while (1) {
        info("UHF task done, delaying forver\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
        uhf_send(uhf_test_message, uhf_test_message_length);
    }
}

static status_t init_stream_buffers() {
    // Init incoming and outgoing streams
    uhfIncomingMessages =
        xStreamBufferCreateStatic(UHF_INCOMING_MESSAGES_STREAM_LENGTH, 1, uhfIncomingMessagesBuffer, &uhfIncomingMessagesStruct);
    if (uhfIncomingMessages == NULL) {
        warning("Failed to create incoming messages stream buffer\n");
        return ERROR_INTERNAL;
    }

    uhfOutgoingMessages =
        xStreamBufferCreateStatic(UHF_OUTGOING_MESSAGES_STREAM_LENGTH, 1, uhfOutgoingMessagesBuffer, &uhfOutgoingMessagesStruct);
    if (uhfOutgoingMessages == NULL) {
        warning("Failed to create outgoing messages stream buffer\n");
        return ERROR_INTERNAL;
    }

    // Init the mutex
    uhfOutgoingMessagesMutex = xSemaphoreCreateMutexStatic(&uhfOutgoingMessagesMutexStruct);
    if (uhfOutgoingMessagesMutex == NULL) {
        warning("Failed to create outgoing messages mutex\n");
        return ERROR_INTERNAL;
    }

    streamsInitialized = 1;
    return SUCCESS;
}

/* --------- Functions below are called by OTHER tasks, not the UHF task ---------
 * This is important to keep in mind when writing/reading these functions
 * i.e. it explains why I use mutexes, since multiple other tasks could be trying to send a message at the same time
 */

static status_t uhf_send_message(char *message, size_t length, bool blocking) {
    if (0 == streamsInitialized) {
        warning("UHF streams not initialized! (is the UHF task running?)\n");
        return ERROR_UNINITIALIZED;
    }

    if (length > UHF_OUTGOING_MESSAGES_STREAM_LENGTH) {
        warning("UHF message too long! UHF_OUTGOING_MESSAGES_STREAM_LENGTH is %d, message length is %d\n",
                UHF_OUTGOING_MESSAGES_STREAM_LENGTH, length);
        return ERROR_MAX_SIZE_EXCEEDED;
    }

    // Aquire the mutex for the outgoing messages buffer
    if (blocking == true) {
        // Allow blocking when aquiring the mutex
        xSemaphoreTake(uhfOutgoingMessagesMutex, portMAX_DELAY);
    } else {
        // Don't allow blocking
        if (pdTRUE != xSemaphoreTake(uhfOutgoingMessagesMutex, 0)) {
            debug("Could not aquire mutex in uhf_send_message -- Someone else has locked it and blocking is not allowed\n");
            return ERROR_RESOURCE_IN_USE;
        }
    }
    // When we get here, the mutex is locked by us (sanity check).
    if (xSemaphoreGetMutexHolder(uhfOutgoingMessagesMutex) != xTaskGetCurrentTaskHandle()) {
        fatal("Mutex should be locked here\n");
    }

    // If blocking is not allowed, make sure we have enough room in the stream
    if (blocking == false) {
        size_t bytes_available = xStreamBufferSpacesAvailable(uhfOutgoingMessages);
        if (bytes_available < length) {
            debug("Not enough space in UHF outgoing buffer for message (space free: %d, message length: %d)\n", bytes_available, length);
            xSemaphoreGive(uhfOutgoingMessagesMutex);
            return ERROR_NO_MEMORY;
        }
    }

    // Now, write into the stream
    size_t bytes_written = 0;
    if (blocking == true) {
        bytes_written = xStreamBufferSend(uhfOutgoingMessages, message, length, portMAX_DELAY);
    } else {
        bytes_written = xStreamBufferSend(uhfOutgoingMessages, message, length, 0);
    }
    if (bytes_written != length) {
        warning("Failed to write entire message to UHF stream buffer (wrote %d/%d | blocking: %d)\n", bytes_written, length, blocking);
        xSemaphoreGive(uhfOutgoingMessagesMutex);
        return ERROR_INTERNAL;
    }

    // Release the mutex
    xSemaphoreGive(uhfOutgoingMessagesMutex);

    return SUCCESS;
}

status_t uhf_send_message_blocking(char *message, size_t length) {
    return uhf_send_message(message, length, true);
}

status_t uhf_send_message_nonblocking(char *message, size_t length) {
    return uhf_send_message(message, length, false);
}