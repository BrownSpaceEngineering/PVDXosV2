#ifndef GLOBALS_H_
#define GLOBALS_H_

enum Status {
    SUCCESS = 0,
    ERROR_NO_DATA,
    ERROR_NO_MEMORY,
    ERROR_UNRECOVERABLE, // If this is returned, the system should restart.
};

#endif // GLOBALS_H_