set pagination off
set confirm off
set print pretty on

define faults
  printf "CFSR=0x%08x HFSR=0x%08x MMFAR=0x%08x BFAR=0x%08x\n", \
    *(unsigned*)0xE000ED28, *(unsigned*)0xE000ED2C, \
    *(unsigned*)0xE000ED34, *(unsigned*)0xE000ED38
end
document faults
Print Cortex-M fault status registers (CFSR/HFSR/MMFAR/BFAR).
end

define rtos_current
  printf "current task: %s\n", ((TCB_t*)pxCurrentTCB)->pcTaskName
end
document rtos_current
Print the name of the currently running FreeRTOS task (names are max 8 chars).
end

define tasks
  info threads
end
document tasks
List FreeRTOS tasks as GDB threads (needs the J-Link FreeRTOS plugin).
end

define reset_halt
  monitor reset 0
  monitor halt
end
document reset_halt
Reset the target and halt at the reset vector (in the bootloader; app symbols won't match yet).
end

define wait_fault
  delete
  break PVDX_default_handler
  continue
  printf "--- stopped in fault handler ---\n"
  bt 12
  faults
  delete
end
document wait_fault
Run until PVDX_default_handler is hit (fatal() resets the board ~1 s after a fault, so a
later attach sees a fresh boot), then print a backtrace and fault registers. Use with a
long GDB_TIMEOUT, e.g. GDB_TIMEOUT=90 tools/gdb.sh wait_fault
end
