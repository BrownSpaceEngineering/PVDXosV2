#include "datastore.h"
#include "rtos_start.h"
#include "semphr.h"
#include "string.h"
#include "globals.h"
#include "watchdog_task.h"

#define MS 10000

typedef struct {
    uint32_t data;
    int time;
    uint32_t negation;
} Data;

// mutex for program, gotten from static semaphore documentation
StaticSemaphore_t xMutexBuffer;
SemaphoreHandle_t xSemaphore = 0;

static Data sensorData[NUM_SENSORS][MAX_STORAGE_SIZE] = {0};
static size_t indices[NUM_SENSORS] = {0}; // tracks the index with the latest data from the specified src

void data_init() {

    // CALL THIS IN HARDWARE_INIT() in bottom of main.c
    // indices of last reading for each sensor
    xSemaphore = xSemaphoreCreateMutexStatic(&xMutexBuffer);  
}



status_t data_in(datasource_t src, uint32_t data, int time) {
    // user must cast data as uint32

//create negation of uint32_t data before putting it into sensorData

//check time to make sure that it is sorted in order (check before and last entries to make sure it is in order)
    
    xSemaphoreTake(xSemaphore, portMAX_DELAY);
    uint32_t negation = ~data;
    sensorData[src][indices[src] % MAX_STORAGE_SIZE] = (Data) {.data = data, .time = time, .negation = negation};
    indices[src] += 1 % MAX_STORAGE_SIZE; // tracks the index with the latest data from the specified src
    xSemaphoreGive(xSemaphore);
    
    return SUCCESS;
}


// int search(datasource_t src, int target_time) {
//     // offsets to account for data wrapping around the array

    
//     int offset = 0;
//     int length = indices[src] + 1;
//     if (indices[src] > MAX_STORAGE_SIZE) {
//         length = MAX_STORAGE_SIZE;
//         offset = indices[src] % MAX_STORAGE_SIZE;
//     }

//     // conduct search without offset, but index with offset
//     int left = 0;
//     int right = length - 1;

//     while (left <= right) {

//         // return index of closer time if time does not match
//         if (right - 1 == left) {
//             // left should be before target time, and right should be after target time
//             int left_diff = target_time - sensorData[src][(left + offset) % MAX_STORAGE_SIZE].time;
//             int right_diff = sensorData[src][(right + offset) % MAX_STORAGE_SIZE].time - target_time;
//             if (left_diff < right_diff) {
//                 return (left + offset) % MAX_STORAGE_SIZE;
//             } else {
//                 return (right + offset) % MAX_STORAGE_SIZE;
//             }
//         }

//         // standard binary search 
//         int mid = (int) (left + (right - left) / 2);
//         if (sensorData[src][(mid + offset) % MAX_STORAGE_SIZE].time == target_time) {

//             return (mid + offset) % MAX_STORAGE_SIZE;
//         } else if (sensorData[src][(mid + offset) % MAX_STORAGE_SIZE].time < target_time) {
//             left = mid;
//         } else {
//             right = mid;
//         }
//     }

status_t data_out(datasource_t src, int target_time, uint32_t* data, int* data_time) {
    // user must uncase the uint32 data

    //check negation of unit32_t data before putting it out
    xSemaphoreTake(xSemaphore, portMAX_DELAY);
    size_t latestDataIndex = indices[src];
    // int index = search(src, target_time);
    Data resultingDataStruct = sensorData[src][latestDataIndex];
    uint32_t denegated = ~resultingDataStruct.negation;

    if (denegated != resultingDataStruct.data) {
        fatal("data is: %d, denegated is: %d. Not equal; Bit flip detected. Rebooting system", resultingDataStruct.data, denegated);
    } else {
        *data = resultingDataStruct.data;
        *data_time = resultingDataStruct.time;
    }

    xSemaphoreGive(xSemaphore);
    return SUCCESS;
}