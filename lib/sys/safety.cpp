/**
 * @file safety.cpp
 *
 * Centralized fault interception for RP2040/RP2350 (Raspberry Pi Pico) systems
 * that mix FreeRTOS on core 0 with bare-metal work on core 1. All fatal
 * conditions funnel through this module so that we get a uniform crash record
 * and a simple post-event console for inspection.
 *
 * Covered fault sources:
 *  - C/C++ assertions (newlib / libc++)
 *  - FreeRTOS configASSERT and safety hooks
 *  - Pico SDK panic()/abort()
 *  - HardFault and other Cortex exception vectors
 *  - Pure-virtual function invocations and std::terminate()
 *
 * Once a fault is captured we freeze the other core, suspend the scheduler (if
 * it is running), and park the system in a watchdog-fed loop that hosts a tiny
 * USB console. The console currently understands a single command: `SHOW`,
 * which replays the captured crash report so that engineers can connect _after_
 * the failure and still retrieve the diagnostics.
 */

#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <new>

#include "safety.hpp"

#include "tusb.h"

#include "pico.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/multicore.h"
#include "pico/status_led.h"
#include "hardware/watchdog.h"
#include "hardware/sync.h"
#include "hardware/structs/psm.h"

#include "FreeRTOS.h"
#include "task.h"

namespace {

constexpr size_t kFaultSourceBufferSize = 32;
constexpr size_t kFaultMessageBufferSize = 192;
constexpr size_t kCommandBufferSize = 16;
constexpr uint32_t kWatchdogResetDelayMs = 100;

struct FaultRecord {
    bool valid{false};
    char source[kFaultSourceBufferSize]{};
    char detail[kFaultMessageBufferSize]{};
    uint32_t core{0};
    absolute_time_t timestamp{};
    bool hasFrame{false};
    T76::Sys::Safety::ExceptionStackFrame frame{};
    uint32_t excReturn{0};
};

FaultRecord g_faultRecord{};
volatile bool g_inFaultHandler = false;
char g_commandBuffer[kCommandBufferSize]{};
size_t g_commandIndex = 0;
bool g_promptShown = false;

void formatMessage(char *buffer, size_t len, const char *fmt, va_list args) {
    if (!buffer || len == 0) {
        return;
    }
    const int written = std::vsnprintf(buffer, len, fmt ? fmt : "<null>", args);
    if (written < 0) {
        buffer[0] = '\0';
    } else if (static_cast<size_t>(written) >= len) {
        buffer[len - 1] = '\0';
    }
}

void resetCommandBuffer() {
    g_commandIndex = 0;
    std::memset(g_commandBuffer, 0, sizeof(g_commandBuffer));
}

bool equalsIgnoreCase(const char *lhs, const char *rhs) {
    while (*lhs && *rhs) {
        if (std::toupper(static_cast<unsigned char>(*lhs)) !=
            std::toupper(static_cast<unsigned char>(*rhs))) {
            return false;
        }
        ++lhs;
        ++rhs;
    }
    return *lhs == '\0' && *rhs == '\0';
}

void captureFaultRecord(const char *source,
                        const char *detail,
                        const T76::Sys::Safety::ExceptionStackFrame *frame,
                        uint32_t excReturn) {
    FaultRecord &record = g_faultRecord;
    record.valid = true;

    const char *effectiveSource = source ? source : "<unknown>";
    std::strncpy(record.source, effectiveSource, sizeof(record.source) - 1);
    record.source[sizeof(record.source) - 1] = '\0';

    if (detail && detail[0] != '\0') {
        std::strncpy(record.detail, detail, sizeof(record.detail) - 1);
        record.detail[sizeof(record.detail) - 1] = '\0';
    } else {
        record.detail[0] = '\0';
    }

    record.core = get_core_num();
    record.timestamp = get_absolute_time();

    if (frame) {
        record.hasFrame = true;
        record.frame = *frame;
        record.excReturn = excReturn;
    } else {
        record.hasFrame = false;
        record.excReturn = 0;
        std::memset(&record.frame, 0, sizeof(record.frame));
    }
}

void holdCoreInReset(uint32_t core) {
    if (core == get_core_num()) {
        return;
    }

    const uint32_t mask = (core == 0) ? PSM_FRCE_OFF_PROC0_BITS : PSM_FRCE_OFF_PROC1_BITS;
    psm_hw->frce_off |= mask;
    while ((psm_hw->frce_off & mask) == 0u) {
        tight_loop_contents();
    }
}

void logTaskState(const FaultRecord &record) {
    if (record.core != 0) {
        std::printf("  Scheduler: bare-metal core (no FreeRTOS context)\r\n");
        return;
    }

    const auto state = xTaskGetSchedulerState();
    if (state == taskSCHEDULER_NOT_STARTED) {
        std::printf("  Scheduler: not started\r\n");
        return;
    }

    const TaskHandle_t current = xTaskGetCurrentTaskHandle();
    const char *name = current ? pcTaskGetName(current) : "<none>";
    std::printf("  Current task: %s\r\n", name ? name : "<unnamed>");

#if (defined(configUSE_TRACE_FACILITY) && (configUSE_TRACE_FACILITY == 1))
    static constexpr UBaseType_t maxTasks = 16;
    TaskStatus_t status[maxTasks];
    UBaseType_t total = uxTaskGetSystemState(status, maxTasks, nullptr);
    std::printf("  Task snapshot (%lu tasks):\r\n", static_cast<unsigned long>(total));
    for (UBaseType_t i = 0; i < total && i < maxTasks; ++i) {
        std::printf("    %lu: %s pri=%lu hw=%lu state=%lu\r\n",
                    static_cast<unsigned long>(i),
                    status[i].pcTaskName ? status[i].pcTaskName : "<unnamed>",
                    static_cast<unsigned long>(status[i].uxCurrentPriority),
                    static_cast<unsigned long>(status[i].usStackHighWaterMark),
                    static_cast<unsigned long>(status[i].eCurrentState));
    }
#else
#if (defined(configUSE_STATS_FORMATTING_FUNCTIONS) && (configUSE_STATS_FORMATTING_FUNCTIONS == 1))
    char buffer[384];
    vTaskGetRunTimeStats(buffer);
    std::printf("  Runtime stats:\r\n%s\r\n", buffer);
#endif
#endif

    if (current) {
        auto watermark = uxTaskGetStackHighWaterMark(current);
        std::printf("  Stack high water mark: %lu words\r\n",
                    static_cast<unsigned long>(watermark));
    }
}

void logExceptionStack(const T76::Sys::Safety::ExceptionStackFrame *frame, uint32_t excReturn) {
    if (!frame) {
        std::printf("  No exception stack frame available\r\n");
        return;
    }

    std::printf("  LR/EXC_RETURN: 0x%08lx\r\n", static_cast<unsigned long>(excReturn));
    std::printf("  R0 : 0x%08lx  R1 : 0x%08lx  R2 : 0x%08lx  R3 : 0x%08lx\r\n",
                static_cast<unsigned long>(frame->r0),
                static_cast<unsigned long>(frame->r1),
                static_cast<unsigned long>(frame->r2),
                static_cast<unsigned long>(frame->r3));
    std::printf("  R12: 0x%08lx  LR : 0x%08lx  PC : 0x%08lx  PSR: 0x%08lx\r\n",
                static_cast<unsigned long>(frame->r12),
                static_cast<unsigned long>(frame->lr),
                static_cast<unsigned long>(frame->pc),
                static_cast<unsigned long>(frame->psr));
}

void printFaultSummary() {
    if (!g_faultRecord.valid) {
        std::printf("No fault recorded.\r\n");
        std::fflush(stdout);
        return;
    }

    const FaultRecord &record = g_faultRecord;

    std::printf("\r\n========== FAULT ==========\r\n");
        std::fflush(stdout);
    std::printf("  Source: %s\r\n", record.source);
        std::fflush(stdout);
    if (record.detail[0] != '\0') {
        std::printf("  Detail: %s\r\n", record.detail);
    }
    std::printf("  Core : %lu\r\n", static_cast<unsigned long>(record.core));
        std::fflush(stdout);
    std::printf("  Time : %llu ms since boot\r\n",
                static_cast<unsigned long long>(to_ms_since_boot(record.timestamp)));
        std::fflush(stdout);

    logTaskState(record);
        std::fflush(stdout);

    if (record.hasFrame) {
        logExceptionStack(&record.frame, record.excReturn);
    } else {
        std::printf("  No exception stack frame captured\r\n");
    }
        std::fflush(stdout);

    std::printf("  Hint : Type SHOW to replay this crash report.\r\n");
    std::fflush(stdout);
}

void handleCommand(const char *command) {
    if (!command || command[0] == '\0') {
        return;
    }

    if (equalsIgnoreCase(command, "SHOW")) {
        printFaultSummary();
    } else {
        std::printf("Unknown command: %s\r\n", command);
        std::fflush(stdout);
    }
}

void processInteractiveCommands() {
    if (!g_promptShown) {
        std::printf("\r\nFault console ready. Type SHOW to replay the report.\r\n> ");
        std::fflush(stdout);
        g_promptShown = true;
    }

    while (true) {
        int raw = getchar_timeout_us(0);
        if (raw < 0) {
            break;
        }

        char c = static_cast<char>(raw);
        if (c == '\r' || c == '\n') {
            std::printf("\r\n");
            g_commandBuffer[g_commandIndex] = '\0';
            if (g_commandIndex > 0) {
                handleCommand(g_commandBuffer);
            }
            resetCommandBuffer();
            std::printf("> ");
            std::fflush(stdout);
            continue;
        }

        if (c == '\b' || c == 127) {
            if (g_commandIndex > 0) {
                --g_commandIndex;
                g_commandBuffer[g_commandIndex] = '\0';
                std::printf("\b \b");
                std::fflush(stdout);
            }
            continue;
        }

        if (std::isprint(static_cast<unsigned char>(c))) {
            if (g_commandIndex < kCommandBufferSize - 1) {
                g_commandBuffer[g_commandIndex++] = c;
                std::putchar(c);
                std::fflush(stdout);
            }
        }
    }
}

[[noreturn]] void enterSafeLoop() {
    watchdog_enable(kWatchdogResetDelayMs, true);
    bool ledOn = true;
    status_led_set_state(ledOn);
    absolute_time_t nextToggle = make_timeout_time_ms(100);
    while (true) {
        watchdog_update();
        tud_task();
        processInteractiveCommands();
        if (absolute_time_diff_us(nextToggle, get_absolute_time()) <= 0) {
            ledOn = !ledOn;
            status_led_set_state(ledOn);
            nextToggle = make_timeout_time_ms(ledOn ? 100 : 200);
        }
        tight_loop_contents();
    }
}

[[noreturn]] void reportFatal(const char *source,
                              const char *detail,
                              const T76::Sys::Safety::ExceptionStackFrame *frame = nullptr,
                              uint32_t excReturn = 0) {

                                printf("[safety] Reporting fatal error from source: %s\r\n", source ? source : "<null>");

    if (g_inFaultHandler) {
        enterSafeLoop();
    }
    g_inFaultHandler = true;

    captureFaultRecord(source, detail, frame, excReturn);

    const bool faultOnCore0 = (g_faultRecord.core == 0);

    printf("Fault on core %u detected. Freezing other core and entering safe loop.\r\n", g_faultRecord.core);

    if (faultOnCore0) {
        holdCoreInReset(1);
    }

    // vTaskSuspendAll();

    // printFaultSummary();

    while(true) {
        status_led_set_state(!status_led_get_state());
        sleep_ms(100);
    }

    std::fflush(stdout);
    std::fflush(stderr);

    // enterSafeLoop();
}

} // namespace

namespace T76::Sys::Safety {

void initialize() {
    std::set_terminate([] {
        reportFatal("std::terminate", "Unhandled C++ exception or terminate() call");
    });

#if __cplusplus < 201703L
    std::set_unexpected([] {
        reportFatal("std::unexpected", "Unexpected exception");
    });
#endif

    std::set_new_handler([] {
        reportFatal("std::new_handler", "Global new_handler invoked");
    });
}

[[noreturn]] void fatal(const char *source, const char *detail) {
    std::fflush(stdout);
    reportFatal(source, detail);
}

[[noreturn]] void fatalWithFrame(const char *source,
                                 const char *detail,
                                 const ExceptionStackFrame *frame,
                                 uint32_t excReturn) {
    reportFatal(source, detail, frame, excReturn);
}

} // namespace T76::Sys::Safety

extern "C" {

static void faultFromFormatted(const char *source, const char *fmt, va_list args) {
    char message[kFaultMessageBufferSize];
    formatMessage(message, sizeof(message), fmt, args);
    T76::Sys::Safety::fatal(source, message);
}

[[noreturn]] void __assert_func(const char *file, int line, const char *func, const char *expr) {
    char message[kFaultMessageBufferSize];
    std::snprintf(message, sizeof(message), "%s:%d %s: %s",
                  file ? file : "<unknown>",
                  line,
                  func ? func : "<global>",
                  expr ? expr : "<null expression>");
    T76::Sys::Safety::fatal("assert", message);
}

[[noreturn]] void __assert_fail(const char *expr, const char *file, unsigned int line, const char *func) {
    __assert_func(file, static_cast<int>(line), func, expr);
}

[[noreturn]] void abort(void) {
    T76::Sys::Safety::fatal("abort", "abort() called");
}

[[noreturn]] void t76_panic(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    faultFromFormatted("panic", fmt, args);
    va_end(args);
    __builtin_unreachable();
}

[[noreturn]] void hard_fault_handler_c(uint32_t *stackPointer, uint32_t excReturn) {
    const auto *frame = reinterpret_cast<T76::Sys::Safety::ExceptionStackFrame *>(stackPointer);
    T76::Sys::Safety::fatalWithFrame("HardFault", "Cortex-M hard fault", frame, excReturn);
}

[[noreturn]] __attribute__((naked)) void HardFault_Handler(void) {
    __asm volatile(
        "tst lr, #4                         \n"
        "ite eq                             \n"
        "mrseq r0, msp                      \n"
        "mrsne r0, psp                      \n"
        "mov   r1, lr                       \n"
        "b     hard_fault_handler_c         \n"
    );
}

[[noreturn]] void Default_Handler(void) {
    T76::Sys::Safety::fatal("Default_Handler", "Unhandled interrupt");
}

[[noreturn]] void NMI_Handler(void) {
    T76::Sys::Safety::fatal("NMI", "Non-maskable interrupt");
}

[[noreturn]] void MemManage_Handler(void) {
    T76::Sys::Safety::fatal("MemManage", "Memory management fault");
}

[[noreturn]] void BusFault_Handler(void) {
    T76::Sys::Safety::fatal("BusFault", "Bus fault");
}

[[noreturn]] void UsageFault_Handler(void) {
    T76::Sys::Safety::fatal("UsageFault", "Usage fault");
}

[[noreturn]] void vAssertCalled(const char *file, int line) {
    char message[kFaultMessageBufferSize];
    std::snprintf(message, sizeof(message), "%s:%d", file ? file : "<unknown>", line);
    T76::Sys::Safety::fatal("configASSERT", message);
}

void vApplicationMallocFailedHook(void) {
    T76::Sys::Safety::fatal("MallocFailedHook", "pvPortMalloc returned null");
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *taskName) {
    char message[kFaultMessageBufferSize];
    std::snprintf(message, sizeof(message), "Task=%s handle=0x%p",
                  taskName ? taskName : "<unnamed>",
                  static_cast<void *>(task));
    T76::Sys::Safety::fatal("StackOverflowHook", message);
}

void vApplicationDaemonTaskStartupHook(void) {
    std::printf("[safety] Daemon task startup hook reached\r\n");
}

[[noreturn]] void __cxa_pure_virtual(void) {
    T76::Sys::Safety::fatal("pure_virtual", "__cxa_pure_virtual invoked");
}

} // extern "C"
