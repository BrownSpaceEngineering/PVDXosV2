#include "datastore.h"
#include "rtos_start.h"
#include "semphr.h"

typedef struct {
    uint32_t data;
    int time;
} Data;

// 2D array of rows of sensor data
static Data sensorData[NUM_SENSORS][MAX_STORAGE_SIZE]; // memset to 0s
// indices of last reading for each sensor
static int indices[NUM_SENSORS] = {0}; // goes past max size?? just use mod

StaticSemaphore_t xMutexBuffer;
SemaphoreHandle_t xSemaphore = xSemaphoreCreateMutexStatic(&xMutexBuffer);  


// how to determine indices for storing/reading data
// locking mechanism
// xSemaphor ... locking 

Status data_in(DataSource src, uint32_t data, int time) {
    // user must cast data as uint32
    sensorData[src][indices[src] % MAX_STORAGE_SIZE] = (Data) {.data = data, .time = time};
    indices[src] += 1;
    return SUCCESS;
}

Status data_out(DataSource src, int target_time, uint32_t* data, int* actual_time) {
    // user must uncase the uint32 data
    int index = search(src, target_time);
    *data = sensorData[src][index].data;
    *actual_time = sensorData[src][index].time;
    return SUCCESS;
}

int search(DataSource src, int target_time) {
    // offsets to account for data wrapping around the array
    int offset = 0;
    int length = indices[src] + 1;
    if (indices[src] > MAX_STORAGE_SIZE) {
        length = MAX_STORAGE_SIZE;
        offset = indices[src] % MAX_STORAGE_SIZE;
    }

    // conduct search without offset, but index with offset
    int left = 0;
    int right = length - 1;

    while (left <= right) {

        // return index of closer time if time does not match
        if (right - 1 == left) {
            // left should be before target time, and right should be after target time
            int left_diff = target_time - sensorData[src][(left + offset) % MAX_STORAGE_SIZE].time;
            int right_diff = sensorData[src][(right + offset) % MAX_STORAGE_SIZE].time - target_time;
            if (left_diff < right_diff) {
                return (left + offset) % MAX_STORAGE_SIZE;
            } else {
                return (right + offset) % MAX_STORAGE_SIZE;
            }
        }

        // standard binary search 
        int mid = (int) (left + (right - left) / 2);
        if (sensorData[src][(mid + offset) % MAX_STORAGE_SIZE].time == target_time) {
            return (mid + offset) % MAX_STORAGE_SIZE;
        } else if (sensorData[src][(mid + offset) % MAX_STORAGE_SIZE].time == target_time) {
            left = mid;
        } else {
            right = mid;
        }
    }
}