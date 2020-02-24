#pragma once

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

    enum class Timeout : uint8_t {
        8MS = 0,
        16MS = 1,
        
    };

    init(const uint32_t timeout);

    wdt_reset();
    mark(const int line, const char* file);
}

#define MARK { const char* file = __SHORT_FILE__; const int line = __LINE__; FeatherFault::mark(line, file); }