#pragma once

#include <stdint.h>
#include "stm32f4xx.h"

/*
 * Declared by system_stm32f4xx.c.
 * Ensure it always matches the actual processor clock.
 */
extern uint32_t SystemCoreClock;

/* Scheduler configuration */
#define configUSE_PREEMPTION 1
#define configUSE_TIME_SLICING 1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_TICKLESS_IDLE 0

#define configCPU_CLOCK_HZ SystemCoreClock
#define configTICK_RATE_HZ 1000U
#define configMAX_PRIORITIES 5U
#define configMINIMAL_STACK_SIZE 128U
#define configMAX_TASK_NAME_LEN 16U
#define configTICK_TYPE_WIDTH_IN_BITS TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD 1

/* Single-core STM32 */
#define configNUMBER_OF_CORES 1

/* Task notifications */
#define configUSE_TASK_NOTIFICATIONS 1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 1

/* Queues and synchronization */
#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 0
#define configUSE_COUNTING_SEMAPHORES 1
#define configUSE_QUEUE_SETS 0
#define configQUEUE_REGISTRY_SIZE 0

/*
 * These are disabled so event_groups.c, stream_buffer.c, and timers.c
 * do not need to be compiled.
 */
#define configUSE_EVENT_GROUPS 0
#define configUSE_STREAM_BUFFERS 0
#define configUSE_TIMERS 0

/* Dynamic allocation using heap_4.c */
#define configSUPPORT_STATIC_ALLOCATION 0
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configTOTAL_HEAP_SIZE (16U * 1024U)
#define configAPPLICATION_ALLOCATED_HEAP 0
#define configSTACK_ALLOCATION_FROM_SEPARATE_HEAP 0
#define configENABLE_HEAP_PROTECTOR 0
#define configHEAP_CLEAR_MEMORY_ON_FREE 0

/* Hooks and diagnostics */
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configUSE_MALLOC_FAILED_HOOK 0
#define configCHECK_FOR_STACK_OVERFLOW 0
#define configGENERATE_RUN_TIME_STATS 0
#define configUSE_TRACE_FACILITY 0
#define configUSE_STATS_FORMATTING_FUNCTIONS 0

/* Standard-library integration */
#define configUSE_NEWLIB_REENTRANT 0

/* Co-routines are not used */
#define configUSE_CO_ROUTINES 0
#define configMAX_CO_ROUTINE_PRIORITIES 1

/* Optional task API functions */
#define INCLUDE_vTaskPrioritySet 0
#define INCLUDE_uxTaskPriorityGet 0
#define INCLUDE_vTaskDelete 1
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_vTaskDelay 1
#define INCLUDE_xTaskDelayUntil 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_xTaskGetCurrentTaskHandle 0
#define INCLUDE_uxTaskGetStackHighWaterMark 0
#define INCLUDE_xTaskGetIdleTaskHandle 0
#define INCLUDE_eTaskGetState 0
#define INCLUDE_xTimerPendFunctionCall 0
#define INCLUDE_xTaskAbortDelay 0
#define INCLUDE_xTaskGetHandle 0
#define INCLUDE_xTaskResumeFromISR 0

// Optional timer API functions
// #define configUSE_TIMERS                1
// #define configTIMER_TASK_PRIORITY       2
// #define configTIMER_QUEUE_LENGTH        5
// #define configTIMER_TASK_STACK_DEPTH    256

/*
 * STM32F411 implements four NVIC priority bits.
 *
 * FreeRTOS-aware interrupts may use priorities 5 through 15.
 * Interrupts at priorities 0 through 4 must not call FreeRTOS APIs.
 */
#define configPRIO_BITS __NVIC_PRIO_BITS
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15U
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5U

#define configKERNEL_INTERRUPT_PRIORITY                                        \
  (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8U - configPRIO_BITS))

#define configMAX_SYSCALL_INTERRUPT_PRIORITY                                   \
  (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8U - configPRIO_BITS))

/*
 * Connect the FreeRTOS Cortex-M port handlers to the names used by the
 * STM32 startup vector table.
 */
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

/* Stop at the failing line so it can be inspected with a debugger. */
#define configASSERT(condition)                                                \
  do {                                                                         \
    if ((condition) == 0) {                                                    \
      __disable_irq();                                                         \
      for (;;) {                                                               \
      }                                                                        \
    }                                                                          \
  } while (0)
