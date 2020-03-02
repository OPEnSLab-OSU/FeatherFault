#pragma once

#include <Arduino.h>
#include <atomic>
#include <Adafruit_ASFcore.h>
#include <reset.h>
#include <sam.h>
#include "ShortFile.h"

/**
 * Welcome to FeatherFault!
 * For more information on how to use this library, please see the [README](./README.md).
 */

namespace FeatherFault {
    /** Enumeration for possible causes for fault */
    enum FaultCause : uint8_t {
        FAULT_NONE = 0,
        /** The watchdog was triggered */
        FAULT_HUNG,
        /** An invalid instruction was executed, or an invalid memory address was accessed */
        FAULT_HARDFAULT,
        /** The heap has crossed into the stack, and the memory is corrupted (see https://learn.adafruit.com/memories-of-an-arduino?view=all) */
        FAULT_OUTOFMEMORY
    };

    enum class WDTTimeout : uint8_t {
        WDT_8MS = 1,
        WDT_15MS = 2,
        WDT_31MS = 3,
        WDT_62MS = 4,
        WDT_125MS = 5,
        WDT_250MS = 6,
        WDT_500MS = 7,
        WDT_1S = 8,
        WDT_2S = 9,
        WDT_4S = 10,
        WDT_8S = 11
    };

    /** Struct containg information about the last fault. */
    struct FaultData {
        /** uin8_t indicating the cause of the fault. */
        FeatherFault::FaultCause cause;
        /** Whether or not the fault happened while FeatherFault was recording line information (1 if true, 0 if not) */
        uint8_t is_corrupted;
        /** Number of times FeatherFault has detected a failure since the device was last programmed */
        uint32_t failnum;
        /** The line number of the last MARK statement before failure (for a memory fault will be the MARK where the fault happened) */
        int32_t line;
        /** The filename where this line was taken from, may be corrupted if is_corrupted is 1 */
        char file[64];
    };

    /**
     * Starts the watchdog timer with a specified timeout. On the event
     * that the watchdog timer runs out (if MARK is not called within
     * the timeout period) a fault will be triggered with cause
     * FeatherFault::FAULT_HUNG and the board will reset.
     * 
     * You do not need to call this function to use FeatherFault,
     * however the FeatherFault::FAULT_HUNG handler will not run
     * without it.
     * 
     * This funtionality is implemented in terms of the early warning
     * interrupt. As a result, the maximum and minimum possible delays
     * for the WDT are not available.
     * @param timeout Timeout to use for the WDT.
     */
    void StartWDT(const WDTTimeout timeout);

    /**
     * Prints information about the fault to a print stream (such as the
     * serial monitor) in a human readable format. This function is 
     * useful if you are debugging with the serial monitor.
     * @param where The print stream to output to (ex. Serial).
     */
    void PrintFault(Print& where);

    /**
     * Returns whether or not FeatherFault has detected a fault since
     * this device was last programmed.
     * @return true if a fault has occurred, false if not.
     */
    bool DidFault();

    /**
     * Returns a FeatherFault::FaultData struct containing information
     * about the last fault to occur. If no fault has occured, this
     * function will return a struct of all zeros.
     */
    FaultData GetFault();

    /** Private utility function called by the MARK macro */
    void _Mark(const int line, const char* file);
}

/** 
 * Macro to track the last place where the program was alive.
 * Place this macro frequently around your code so FeatherFault
 * knows where a fault happened. For example:
 * ```C++
 * MARK;
 * while (sketchyFunction()) {
 *   MARK;
 *   moreSketchyThings(); MARK;
 * }
 * ```
 * Every call to MARK will store the current line # and filename
 * to some global varibles, allowing FeatherFault to determine
 * where the failure happened when the program faults.
 * 
 * This macro is a proxy for FeatherFault::_Mark, allowing it to 
 * grab the line # and filename.
 */
#define MARK { FeatherFault::_Mark(__LINE__,  __SHORT_FILE__); }