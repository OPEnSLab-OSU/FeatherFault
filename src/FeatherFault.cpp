#include "FeatherFault.h"

/** Allocate 512 bytes in flash for our crash logs */
__attribute__((__aligned__(256))) static volatile const uint8_t FeatherFaultFlash[256] = {};

struct FaultDataPrivate {
    char marker[31] = "FeatherFault Data Here! Cause:";
    FeatherFault::FaultCause cause;
    char marker2[8] = "My Bad:";
    uint8_t mybad;
    char marker3[8] = "Line #:";
    int32_t line;
    char marker4[6] = "File:";
    char file[64]; // may be corrupted if mybad is true
};

typedef union {
    struct FaultDataPrivate data;
    uint32_t raw[(sizeof(FaultDataPrivate)+3)/4]; // rounded to the nearest 4 bytes
    uint8_t debug_raw[sizeof(FaultDataPrivate)];
} FaultData_t;

static const uint32_t pageSizes[] = { 8, 16, 32, 64, 128, 256, 512, 1024 };

/** Global atomic bool to specify that last_line or last_file are being written to */
static volatile std::atomic_bool is_being_written(false);
/** Global varible to store the last line MARKed. Do not change manually! */
static volatile int last_line = 0;
/** Global varible to store the last filename. Do not change manually! */
static volatile const char* last_file = "";

/**
 * Write fault data to flash
 */
void FeatherFault::HandleFault(const FeatherFault::FaultCause cause) {
    // TODO: read the stack?
    // Create a fault data object, and populate it
    FaultData_t trace = { {} };
    // check if FeatherFault may have been the cause (oops)
    trace.data.mybad = is_being_written.load() ? 1 : 0;
    // write cause, line, and file info
    trace.data.cause = cause;
    trace.data.line = last_line;
    // if the pointer was being written and we interrupted it, we don't want to make things worse
    if (!trace.data.mybad) {
        const volatile char* index = last_file;
        uint32_t i = 0;
        for (; i < sizeof(trace.data.file) - 1 && *index != '\0'; i++)
            trace.data.file[i] = *(index++);
        trace.data.file[i] = '\0';
    }
    else 
        trace.data.file[0] = '\0'; // Corrupted!
    // Write that fault data object to flash
    volatile void* flash_unsafe = (volatile void*)FeatherFaultFlash;
    volatile uint32_t* flash_safe = (volatile uint32_t*)flash_unsafe;
    for (uint32_t i = 0; i < sizeof(trace.raw) / 4; i++) {
        // write!
        *(flash_safe++) = trace.raw[i];
    }
    NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_WP;
}

static void WDTReset() {
    while(WDT->STATUS.bit.SYNCBUSY);
    WDT->CLEAR.reg = WDT_CLEAR_CLEAR_KEY;
}

/**
 * @brief WDT Handler, saves the last state to flash before the board is reset.
 */
[[ noreturn ]] __attribute__ ((interrupt ("IRQ"))) void WDT_Handler() {
    WDT->INTFLAG.bit.EW  = 1;        // Clear interrupt flag
    // Handle fault!
    HandleFault(FeatherFault::FAULT_HUNG);
    // manually reset the board 
    // while(true) {}
}

void FeatherFault::Init(const FeatherFault::WDTTimeout timeout) {
    // Generic clock generator 2, divisor = 32 (2^(DIV+1))
    GCLK->GENDIV.reg = GCLK_GENDIV_ID(2) | GCLK_GENDIV_DIV(4);
    // Enable clock generator 2 using low-power 32KHz oscillator.
    // With /32 divisor above, this yields 1024Hz(ish) clock.
    GCLK->GENCTRL.reg = GCLK_GENCTRL_ID(2) |
                        GCLK_GENCTRL_GENEN |
                        GCLK_GENCTRL_SRC_OSCULP32K |
                        GCLK_GENCTRL_DIVSEL;
    while(GCLK->STATUS.bit.SYNCBUSY);
    // WDT clock = clock gen 2
    GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID_WDT |
                        GCLK_CLKCTRL_CLKEN |
                        GCLK_CLKCTRL_GEN_GCLK2;
    // Enable WDT early-warning interrupt
    NVIC_DisableIRQ(WDT_IRQn);
    NVIC_ClearPendingIRQ(WDT_IRQn);
    NVIC_SetPriority(WDT_IRQn, 0); // Top priority
    NVIC_EnableIRQ(WDT_IRQn);
    // Disable watchdog for config
    WDT->CTRL.reg = 0;
    while(WDT->STATUS.bit.SYNCBUSY);
    // Enable early warning interrupt
    WDT->INTENSET.bit.EW   = 1;
    // Period = twice
    WDT->CONFIG.bit.PER    = static_cast<uint8_t>(timeout);
    Serial.println(WDT->CONFIG.bit.PER);
    // Set time of interrupt 
    WDT->EWCTRL.bit.EWOFFSET = static_cast<uint8_t>(timeout) - 1;
    Serial.println(WDT->EWCTRL.bit.EWOFFSET);
    // Disable window mode
    WDT->CTRL.bit.WEN      = 0;
    // Sync CTRL write
    while(WDT->STATUS.bit.SYNCBUSY); 
    // Clear watchdog interval
    WDTReset();
    // Start watchdog now!  
    WDT->CTRL.bit.ENABLE = 1;            
    while(WDT->STATUS.bit.SYNCBUSY);
}

/**
 * @brief Save a location in the file, and reset the watchdog timer
 */
void FeatherFault::Mark(const int line, const char* file) {
    WDTReset();
    is_being_written.store(true);
    last_line = line;
    last_file = file;
    is_being_written.store(false);
}

/**
 * @brief Print information on the fault to somewhere
 */
void FeatherFault::PrintFault(Print& where) {
    // Load the fault data from flash
    FaultData_t trace = { {} };
    memcpy(&trace.raw, (const void*)FeatherFaultFlash, sizeof(trace));
    // print it the printer
    if (trace.data.cause != FeatherFault::FAULT_NONE) {
        where.print("Fault! Cause: ");
        switch (trace.data.cause) {
            case FeatherFault::FAULT_HUNG: where.println("HUNG"); break;
            case FeatherFault::FAULT_HARDFAULT: where.println("HARDFAULT"); break;
            case FeatherFault::FAULT_STACKOVERFLOW: where.println("STACKOVERFLOW"); break;
            default: where.println("Corrupted");
        }
        where.print("FeatherFault's error: ");
        where.println(trace.data.mybad ? "Yes" : "No");
        where.print("Line: ");
        where.println(trace.data.line);
        where.print("File: ");
        where.println(trace.data.file);
    }
    else
        where.println("No fault");
}