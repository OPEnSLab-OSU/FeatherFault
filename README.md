# 🔬FeatherFault

[![Build Status](https://travis-ci.org/OPEnSLab-OSU/FeatherFault.svg?branch=master)](https://travis-ci.org/OPEnSLab-OSU/FeatherFault)

When a microcontroller crashes or hangs, it can be quite difficult to troubleshoot what caused it. FeatherFault is an attempt to build a system that can not only recover from a crash, but explain why the crash happened. FeatherFault supports all boards using the SAMD21 (Adafruit Feather M0, Arduino Zero, etc.), and future support is planned for the SAMD51.

## Getting Started

FeatherFault can be installed through the Arduino Library Manager, or by downloading this repository. The Adafruit ASF core is also required, which can be found [here](https://github.com/adafruit/Adafruit_ASFcore/tree/f6ffa8b2bc2477566c8406e5f3fa883b137347f1). Once these are both installed, FeatherFault can be activated by adding the following lines to the beginning of a sketch:
```C++
#include "FeatherFault.h"

void setup() {
    Serial.begin(...);
    while(!Serial);
    FeatherFault::PrintFault(Serial);
    Serial.flush();
    FeatherFault::StartWDT(FeatherFault::WDTTimeout::WDT_8S);
    ...
}
```
and decorating the sketch code with `MARK` statements, making sure to surround suspicious code sections with them. `MARK` may not be used more than once per line, and must be used both before and after the suspected code:
```C++
void loop() {
    // Mark a function
    MARK; 
    do_something_suspicous(); 
    MARK;

    // Mark a loop
    MARK;
    while (unsafe_function_one() == true) { MARK;
        // Ignore safe functions, but mark the unsafe ones
        // Which functions are 'unsafe' is up to the programmer
        safe_function_one();
        safe_function_two();
        safe_function_three();
        MARK;
        unsafe_function_two();
        MARK;
    }
}
```

Once FeatherFault is activated, it will trigger after a set time of inactivity (we specify 8 seconds above, but this value can be changed), on [memory overflow](https://learn.adafruit.com/memories-of-an-arduino?view=all), or on a [hard fault](https://www.freertos.org/Debugging-Hard-Faults-On-Cortex-M-Microcontrollers.html). Once triggered, FeatherFault will immediately save the location of the last run `MARK` statement along with the fault cause, and reset the board. This saved data can then be read by `FeatherFault::PrintFault` and `FeatherFault::GetFault`, allowing the developer to determine if the board has failed after it resets.

### Usage Example

To show how this behavior works, let's assume that `unsafe_function()` in the code block below attempts to access memory that doesn't exist, causing a hard fault:
```C++
void setup() {
    // Wait for serial to connect to the serial monitor
    Serial.begin(...);
    while(!Serial);
    // begin code
    Serial.println("Start!");
    other_function_one();
    unsafe_function(); // oops
    other_function_two();
    Serial.println("Done!");
}
```
If we run this code without FeatherFault, we would see the serial monitor output something like this:
```
Start!
```
After which the device hard faults, causing it to wait in an infinite loop until it is reset. 

This behavior is extremely difficult to troubleshoot: as the developer, all we know is that the device failed between `Start!` and `Done`. Using more print statements, we could eventually narrow down the cause to `unsafe_function`—this process is time consuming, unreliable, and downright annoying. Instead, let's try the same code with FeatherFault activated:
```C++
void setup() {
    // Wait for serial to connect to the serial monitor
    Serial.begin(...);
    while(Serial);
    // Activate FeatherFault
    FeatherFault::PrintFault(Serial);
    FeatherFault::StartWDT(FeatherFault::WDTTimeout::WDT_8S);
    // begin code
    MARK;
    Serial.println("Start!");
    MARK;
    other_function_one();
    MARK;
    unsafe_function(); // oops
    MARK;
    other_function_two();
    MARK;
    Serial.println("Done!");
    MARK;
}
```
Running that sketch, we would see the following serial monitor output:
```
No fault
Start!
```
`No fault` here indicates that FeatherFault has not been triggered yet. We change that shortly by running `unsafe_function()`, causing a hard fault. Instead of waiting in an infinite loop, however, the board is immediately reset to the start of the sketch by FeatherFault. We can then open the serial monitor again:
```
Fault! Cause: HARDFAULT
Fault during recording: No
Line: 18
File: MySketch.ino
Failures since upload: 1
Start!
```
Since the FeatherFault was triggered by the hard fault, `FeatherFault::PrintFault` will print the last file and line number `MARK`ed before the hard fault happened. In this case, line 18 of `MySketch.ino` indicates the `MARK` statement after `other_function_one()`, leading us to suspect that `unsafe_function()` is causing the issue. We can now focus on troubleshooting `unsafe_function()`.

## Additional Features

### Getting Fault Data In The Sketch

While most projects should only need traces on the serial monitor, some (such as remote deployments) will need to log the data to other mediums. To do this, FeatherFault has the `FeatherFault::DidFault` and `FeatherFault::GetFault` functions to check if a fault has occurred, and to get the last fault trace. For more information on these functions, please see [FeatherFault.h](./src/FeatherFault.h).

### Getting Fault Data Without Serial

If a serial connection cannot be established while the sketch is running, but the board is able to communicate in bootloader mode, the [recover_fault python script](./tools/recover_fault/recover_fault.py) can download and read FeatherFault trace data using the bootloader. Simply follow the setup instructions contained in the script, reset the board into bootloader mode, and run:
```
python ./recover_fault.py recover <comport>
```

### Running Code When The Device Faults

Some code may be needed to perform cleanup of external devices after FeatherFault causes an unexpected reset. There are two general method for this: a safe one, and an unsafe one. While the safe method is generally recommended, access to the state of the program may be needed during the fault,in which case the unsafe method is necessary.

#### Safe Method

The safe method uses `FeatherFault::DidFault` at the beginning of setup:
```C++
void setup() {
    ...
    if (FeatherFault::DidFault()) {
        // perform cleanup here
        cleanup_code();
    }
    ...
}
```
Since FeatherFault resets the board immediately upon failure, `cleanup_code()` will run every time FeatherFault is triggered. When writing the `cleanup_code()` routine, remember that the program state has been entirely cleared, and any devices or variables in the sketch must be initialized before they can be used (ex. `Serial.begin` must be called to use Serial). If access to a variable value before the device is reset is needed, please see the unsafe method below.

#### Unsafe Method

The unsafe method uses `FeatherFault::SetCallback` to register a function to be called before the device is reset:
```C++
volatile void cleanup_code() {
    // perform cleanup here
    // can also read global variables
}

void setup() {
    ...
    FeatherFault::SetCallback(cleanup_code);
    ...
}
```
`cleanup_code()` will be called after FeatherFault stores a trace, but before the device is reset—allowing it to access global variables and devices in the faulted state. Note that this implementation has a few major caveats:
 * The callback (`cleanup_code`) must be [interrupt safe](https://www.arduino.cc/reference/en/language/functions/external-interrupts/attachinterrupt/) (cannot use `delay`, `Serial`, etc.).
 * The callback must be *extremely careful* when accessing memory outside of itself. All memory should be assumed corrupted unless proven otherwise. Pointers should be treated with extra caution.
 * The callback must execute in less time than the specified WDT timeout, or it will be reset by the watchdog timer.
 * If the callback itself faults, an infinite loop will be triggered.

Because of the above restrictions, it is *highly* recommended that the safe method is used wherever possible.

## Implementation Notes

FeatherFault currently handles three failure modes: hanging, [memory overflow](https://learn.adafruit.com/memories-of-an-arduino?view=all), and [hard fault](https://www.freertos.org/Debugging-Hard-Faults-On-Cortex-M-Microcontrollers.html). When any of these failure modes are triggered, FeatherFault will immediately write the information from the last `MARK` to flash memory, and cause a system reset. `FeatherFault::PrintFault`, `FeatherFault::GetFault`, and `FeatherFault::DidFault` read this flash memory to retrieve information regarding the last fault.

#### Hanging Detection

Hanging detection is implemented using the SAMD watchdog timer early warning interrupt. As a result, FeatherFault will not detect hanging unless `FeatherFault::StartWDT` is called somewhere in the beginning of the sketch. Note that similar to normal watchdog operation, FeatherFaults detection must be periodically resetting using `MARK` macro; this means that the `MARK` macro must be placed such that it is called at least periodically under the timeout specified. In long operations that cannot be `MARK`ed (sleep being an example), use `FeatherFault::StopWDT` to disable the watchdog during that time.

Behind the scenes watchdog feeding is implemented in terms of a global atomic boolean which determines if the device should fault during the watchdog interrupt, as opposed to the standard register write found in SleepyDog and other libraries. This decision was made because feeding the WDT on the SAMD21 is [extremely slow (1-5ms)](https://www.avrfreaks.net/forum/c21-watchdog-syncing-too-slow), which is unacceptable for the `MARK` macro (see https://github.com/OPEnSLab-OSU/FeatherFault/issues/4). Note that due to this implementation, the watchdog interrupt happens regularly and may take an extended period of time (1-5ms), causing possible timing issues with other code.

#### Memory Overflow Detection

Memory overflow detection is implemented by checking the top of the heap against the top of the stack. If the stack is overwriting the heap, memory is assumed to be corrupted and the board is immediately reset. This check is performed inside the `MARK` macro.

#### Hard Fault Detection

Hard Fault detection is implemented using the existing hard fault interrupt vector built into ARM. This interrupt is normally [defined as a infinite loop](https://github.com/adafruit/ArduinoCore-samd/blob/bf24e95f7ef7b41201d4389ef47b858b14ca58dd/cores/arduino/cortex_handlers.c#L43), however FeatherFault overrides this handler to allow for tracing and a graceful recovery. This feature is activated when FeatherFault is included in the sketch.

