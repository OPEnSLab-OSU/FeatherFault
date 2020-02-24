#include "FeatherFault.h"

/** Allocate 512 bytes in flash for our crash logs */
__attribute__((__aligned__(256))) static volatile const uint8_t FeatherFaultFlash[256] = {};

struct FaultDataPrivate {
    const char marker[] = "FeatherFault Data Here:";
    FaultCause cause;
    int line;
    char file[64];
};

typedef union {
    struct FaultDataPrivate data;
    uint32_t raw[(sizeof(FaultDataPrivate)+3)/4*4]; // rounded to the nearest 4 bytes
} FaultData_t;

static const uint32_t pageSizes[] = { 8, 16, 32, 64, 128, 256, 512, 1024 };

/** Global varible to store the last line MARKed. Do not change manually! */
static int last_line = 0;
/** Global varible to store the last filename. Do not change manually! */
static const char* last_file = "";

/**
 * @brief WDT Handler, saves the last state to flash before the board is reset.
 */
void WDT_Handler(void) {
    WDT->INTFLAG.bit.EW  = 1;        // Clear interrupt flag
    // TODO: read the stack?
    // Create a fault data object, and populate it
    FaultData_t trace;
    trace.data.cause = FeatherFault::FAULT_HUNG;
    trace.data.line = last_line;
    {
        const char* index = last_file;
        size_t i = 0;
        for (; i < sizeof(trace.data.file) - 1 && *index != '\0'; i++)
            trace.data.file[i] = *(index++);
        trace.data.file[i] = '\0';
    }
    // Write that fault data object to flash
    volatile void* flash_unsafe = (volatile void*)FeatherFaultFlash;
    volatile uint32_t* flash_safe = (volatile uint32_t*)flash_unsafe;
    // determine page size
    const uint32_t pagesize = pageSizes[NVMCTRL->PARAM.bit.PSZ];
    // Disable automatic page write
    NVMCTRL->CTRLB.bit.MANW = 1;
    // iterate!
    size_t new_page_idx = pagesize / 4;
    // flush buffer to start
    NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_WP;
    while (NVMCTRL->INTFLAG.bit.READY == 0) { }
    for (size_t i = 0; i < sizeof(FaultData_t) / 4; i++) {
        // write!
        *(flash_safe++) = trace.raw[i];
        // flush the page if needed (every pagesize words and the last run)
        if (new_page_idx == 0 || i == (sizeof(FaultData_t) / 4 - 1)) {
            // flush buffer
            NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_WP;
            while (NVMCTRL->INTFLAG.bit.READY == 0) { }
            // Execute "PBC" Page Buffer Clear
            NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_PBC;
            while (NVMCTRL->INTFLAG.bit.READY == 0) { }
            new_page_idx = pagesize / 4;
        }
    }
    // All done! the chip will now reset
}

void FeatherFault::init() {
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
    WDT->CONFIG.bit.PER    = bits+1;
    // Set time of interrupt 
    WDT->EWCTRL.bit.EWOFFSET = bits;
    // Disable window mode
    WDT->CTRL.bit.WEN      = 0;
    // Sync CTRL write
    while(WDT->STATUS.bit.SYNCBUSY); 
    // Clear watchdog interval
    reset();
    // Start watchdog now!  
    WDT->CTRL.bit.ENABLE = 1;            
    while(WDT->STATUS.bit.SYNCBUSY);
}

/**
 * @brief Save a location in the file, and reset the watchdog timer
 */
void FeatherFault::mark(const int line, const char* file) {
    wdt_reset();
    last_line = line;
    last_file = file;
}