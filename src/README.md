# Source

This is where most of the code for PVDXos lives. The `tasks` folder contains the various tasks that run on the scheduler, while `drivers` contains the code that interfaces with various peripheral devices.

## Key Details/Terms

- **Makefile** - the Makefile at this level is mostly intended to be called by the outer Makefile, but contains the command to update the ASF folder based on a new .atzip file.

- **FreeRTOS** - a library for building real-time operating systems which manages task scheduling for us; that is, it provides a scheduler which determines which of our tasks to run and when based on their assigned priorities.

- **ASF** - The ASF folder on the top level provides the library code for a variety of purposes, including interfacing with the pins on the SAMD51 and the FreeRTOS functions we use. It is configured and updated via Atmel .atzip files, which we use [this online tool](https://start.atmel.com/) to deal with.
