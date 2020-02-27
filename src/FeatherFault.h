#pragma once

#include <Arduino.h>
#include <atomic>
#include <Adafruit_ASFcore.h>
#include <reset.h>
#include <sam.h>
#include "ShortFile.h"

/**
 * Welcome to FeatherFault!
 * Everything here is stored in a global namespace, because we have to do some
 * hinky things with global varibles
 */

namespace FeatherFault {
    enum FaultCause : uint8_t {
        FAULT_NONE = 0,
        FAULT_HUNG,
        FAULT_HARDFAULT,
        FAULT_STACKOVERFLOW
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

    void Init(const WDTTimeout timeout);
    void Mark(const int line, const char* file);
    void PrintFault(Print& where);
    // TODO: Return the fault data as actual values, instead of just printing

    // For testing
    void HandleFault(const FeatherFault::FaultCause cause);
}

// TODO: Production mode to remove these values
#define MARK { FeatherFault::Mark(__LINE__,  __SHORT_FILE__); }