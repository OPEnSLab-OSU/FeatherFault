#include "FeatherFault.h"

/** Allocate 512 bytes in flash for our crash logs */
alignas(256) _Pragma("location=\"FLASH\"") static const uint8_t FeatherFaultFlash[256] = { 0 };
const void* FeatherFaultFlashPtr = FeatherFaultFlash;

/**
 * Struct similar to FeatherFault::FaultData, but with strings to mark
 * where data is stored in a flash dump. All properties except mark*
 * have the same meaning as FeatherFault::FaultData.
 * 
 * Please ensure that all values in this struct are word (4-byte) aligned, to
 * prevent decoding issues when reading directly from flash.
 */
struct alignas(uint32_t) FaultDataFlashStruct {
    uint32_t value_head = 0xFEFEFAFA;
    char marker[32] = "FeatherFault Data Here! Caused:";
    uint32_t cause; // FeatherFaultFlashStruct + 36
    char marker2[8] = "My Bad:";
    uint32_t is_corrupted; // FeatherFaultFlashStruct + 48
    char marker3[8] = "Fail #:";
    uint32_t failnum; // 
    char marker4[8] = "Line #:";
    int32_t line;
    char marker5[8] = "File n:";
    char file[64]; // may be corrupted if is_corrupted is true
};

typedef union {
    struct FaultDataFlashStruct data;
    uint32_t alignas(FaultDataFlashStruct) raw_u32[(sizeof(FaultDataFlashStruct)+3)/4]; // rounded to the nearest 4 bytes
    uint8_t alignas(FaultDataFlashStruct) raw_u8[sizeof(FaultDataFlashStruct)];
} FaultDataFlash_t;

static const uint32_t pageSizes[] = { 8, 16, 32, 64, 128, 256, 512, 1024 };

/** Global atomic bool to specify that last_line or last_file are being written to, determines if a fault happened while they were being written */
static volatile std::atomic_bool is_being_written(false);
/** Global varible to store the last line MARKed, written by FeatherFault::_Mark and read by FeatherFault::HandleFault */
static volatile int last_line = 0;
/** Global varible to store the last filename, written by FeatherFault::_Mark and read by FeatherFault::HandleFault */
static volatile const char* last_file = "";

#ifdef __arm__
// should use uinstd.h to define sbrk but Due causes a conflict
extern "C" char* sbrk(int incr);
#else  // __ARM__
extern char *__brkval;
#endif  // __arm__

static int freeMemory() {
  char top;
#ifdef __arm__
  return &top - reinterpret_cast<char*>(sbrk(0));
#elif defined(CORE_TEENSY) || (ARDUINO > 103 && ARDUINO != 151)
  return &top - __brkval;
#else  // __arm__
  return __brkval ? &top - __brkval : &top - __malloc_heap_start;
#endif  // __arm__
}

/**
 * @brief Generic fault handler for FeatherFault
 * 
 * Performs the following steps in order:
 * 1. Determines if the fault happened while FeatherFault was recording line information.
 * If so, the information could be corrupted, so we don't record it to prevent faulting
 * again. If the fault happened any other time, get the line information from our global
 * varibles last_line and last_file.
 * 2. Write the data we just recorded and the fault cause to flash, at the address pointed 
 * to by FeatherFaultFlash. In order to do this, we must first erase the flash (not sure why), 
 * then write to it. Most of the code for this is ensuring that all flash operations are 
 * page aligned.
 * 3. Reset the system using NVIC_SystemReset.
 * 
 * @param cause The reason HandleFault was called (should not be FAULT_NONE).
 * @return This function does not return.
 */
[[ noreturn ]] static void HandleFault(const FeatherFault::FaultCause cause) {
    // TODO: read the stack?
    // Create a fault data object, and populate it
    FaultDataFlash_t trace = { {} };
    // check if FeatherFault may have been the cause (oops)
    trace.data.is_corrupted = is_being_written.load() ? 1 : 0;
    // write cause, line, and file info
    trace.data.cause = static_cast<uint32_t>(cause);
    trace.data.line = last_line;
    // if the pointer was being written and we interrupted it, we don't want to make things worse
    if (!trace.data.is_corrupted) {
        const volatile char* index = last_file;
        uint32_t i = 0;
        for (; i < sizeof(trace.data.file) - 1 && *index != '\0'; i++)
            trace.data.file[i] = *(index++);
        trace.data.file[i] = '\0';
    }
    else 
        trace.data.file[0] = '\0'; // Corrupted!
    // read the failure number from flash, and write it + 1
    trace.data.failnum = ((FaultDataFlash_t*)FeatherFaultFlashPtr)->data.failnum + 1;
    // Write that fault data object to flash
    volatile uint32_t* flash_u32 = (volatile uint32_t*)FeatherFaultFlashPtr;
    volatile uint8_t* const flash_u8 = (volatile uint8_t*)FeatherFaultFlashPtr;
    // determine page size
    const uint32_t pagesize = pageSizes[NVMCTRL->PARAM.bit.PSZ];
    // iterate!
    const size_t pagewords = pagesize / 4;
    const size_t pagerows = pagesize * 4;
    // erase our memory
    for (size_t i = 0; i < sizeof(trace.raw_u32); i += pagerows) {
        NVMCTRL->ADDR.reg = ((uint32_t)&(flash_u8[i])) / 2;
        NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_ER;
        while (!NVMCTRL->INTFLAG.bit.READY) { }
    }
    // Disable automatic page write
    NVMCTRL->CTRLB.bit.MANW = 1;
    // iterate!
    const size_t writelen = sizeof(trace.raw_u32) / 4;
    size_t idx = 0;
    while (idx < writelen) {
        // Execute "PBC" Page Buffer Clear
        NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_PBC;
        while (NVMCTRL->INTFLAG.bit.READY == 0) { }
        // write!
        const size_t min_idx = (writelen - idx) > pagewords ? pagewords : (writelen - idx);
        for (size_t i = 0; i < min_idx; i++)
            *(flash_u32++) = trace.raw_u32[idx++];
        // flush the page if needed (every pagesize words and the last run)
        NVMCTRL->CTRLA.reg = NVMCTRL_CTRLA_CMDEX_KEY | NVMCTRL_CTRLA_CMD_WP;
        while (NVMCTRL->INTFLAG.bit.READY == 0) { }
    }
    // All done! the chip will now reset
    NVIC_SystemReset();
    while(true);
}

static void WDTReset() {
    while(WDT->STATUS.bit.SYNCBUSY);
    WDT->CLEAR.reg = WDT_CLEAR_CLEAR_KEY;
}

/** WDT Handler, calls HandleFault(FeatherFault::FAULT_HUNG) */
[[ noreturn ]] __attribute__ ((interrupt ("IRQ"))) void WDT_Handler() {
    WDT->INTFLAG.bit.EW  = 1;        // Clear interrupt flag
    // Handle fault!
    HandleFault(FeatherFault::FAULT_HUNG);
}

/** HardFault Handler, calls HandleFault(FeatherFault::FAULT_HARDFAULT) */
[[ noreturn ]] __attribute__ ((interrupt ("IRQ"))) void HardFault_Handler() {
    // Handle fault! Hope we can still execute code
    HandleFault(FeatherFault::FAULT_HARDFAULT);
}

/* See FeatherFault.h */
void FeatherFault::StartWDT(const FeatherFault::WDTTimeout timeout) {
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
    // Set time of interrupt 
    WDT->EWCTRL.bit.EWOFFSET = static_cast<uint8_t>(timeout) - 1;
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

void FeatherFault::StopWDT() {
    // stop the watchdog
    WDT->CTRL.bit.ENABLE = 0;            
    while(WDT->STATUS.bit.SYNCBUSY);
}

/* See FeatherFault.h */
void FeatherFault::_Mark(const int line, const char* file) {
    WDTReset();
    is_being_written.store(true);
    last_line = line;
    last_file = file;
    is_being_written.store(false);
    // check for a stackoverflow
    const int mem = freeMemory();
    if (mem < 0 || mem > 60000)
        HandleFault(FeatherFault::FAULT_OUTOFMEMORY);
}

/* See FeatherFault.h */
void FeatherFault::PrintFault(Print& where) {
    // Load the fault data from flash
    const FaultDataFlash_t* trace = (FaultDataFlash_t*)FeatherFaultFlashPtr;
    // print it the printer
    if (trace->data.cause != FeatherFault::FAULT_NONE) {
        where.print("Fault! Cause: ");
        switch (trace->data.cause) {
            case FeatherFault::FAULT_HUNG: where.println("HUNG"); break;
            case FeatherFault::FAULT_HARDFAULT: where.println("HARDFAULT"); break;
            case FeatherFault::FAULT_OUTOFMEMORY: where.println("OUTOFMEMORY"); break;
            default: where.println("Corrupted");
        }
        where.print("Fault during recording: ");
        where.println(trace->data.is_corrupted ? "Yes" : "No");
        where.print("Line: ");
        where.println(trace->data.line);
        where.print("File: ");
        where.println(trace->data.file);
        where.print("Failures since upload: ");
        where.println(trace->data.failnum);
    }
    else
        where.println("No fault");
}

/* See FeatherFault.h */
bool FeatherFault::DidFault() {
    // Load the fault data from flash
    const FaultDataFlash_t* trace = (FaultDataFlash_t*)FeatherFaultFlashPtr;
    return trace->data.cause != FeatherFault::FAULT_NONE;
}

/* See FeatherFault.h */
FeatherFault::FaultData FeatherFault::GetFault() {
    // Load the fault data from flash
    const FaultDataFlash_t* trace = (FaultDataFlash_t*)FeatherFaultFlashPtr;
    // copy all relavent data
    FaultData ret;
    ret.cause = static_cast<FeatherFault::FaultCause>(trace->data.cause);
    ret.is_corrupted = trace->data.is_corrupted;
    ret.failnum = trace->data.failnum;
    ret.line = trace->data.line;
    memcpy(ret.file, trace->data.file, sizeof(ret.file));
    return ret;
}