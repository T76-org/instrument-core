#include <FreeRTOS.h>
#include <task.h>


void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName) {
    ( void ) pcTaskName;
    ( void ) pxTask;

    /* Run time stack overflow checking is performed if
    configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2.  This hook
    function is called if a stack overflow is detected. */

    /* Force an assert. */
    configASSERT( ( volatile void * ) NULL ); //TODO: replace with custom error handler
}
