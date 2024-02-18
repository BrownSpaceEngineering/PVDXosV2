void perform_flip();
void cosmicmonkey_main(void *pvParameters);
#define COSMICMONKEY_TASK_STACK_SIZE 128

struct cosmicmonkeyTaskMemory {
    StackType_t OverflowBuffer[TASK_STACK_OVERFLOW_PADDING];
    StackType_t cosmicmonkeyTaskStack[COSMICMONKEY_TASK_STACK_SIZE];
    StaticTask_t cosmicmonkeyTaskTCB;
};
extern struct cosmicmonkeyTaskMemory cosmicmonkeyMem;