# 🔬FeatherFault

When a microcontroller crashes or hangs, it can be quite difficult to troubleshoot what caused it. FeatherFault is an attempt to build a system that can not only recover from a crash, but tell you why the crash happened. FeatherFault supports all boards using the SAMD21 (Adafruit Feather M0, Arduino Zero, etc.), and future support is planned for the SAMD51.

## Getting Started

You can install FeatherFault through the Arduino Library Manager, or by downloading this repository. Once installed, you can activate FeatherFault by adding the following lines to the beginning of your setup
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
and decorating your code with `MARK` statements, making sure to surround suspicious code sections with them. `MARK` may not be used more than once per line, and must be used both before and after the suspected code:
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

Once FeatherFault is activated, it will trigger after a set time of inactivity (we specify 8 seconds above, but you can change the value if you like), on [memory overflow](https://learn.adafruit.com/memories-of-an-arduino?view=all), or on a [hard fault](https://www.freertos.org/Debugging-Hard-Faults-On-Cortex-M-Microcontrollers.html). Once triggered, FeatherFault will immediately save the location of the last run `MARK` statement along with the fault cause, and reset the board. This saved data can then be read by `FeatherFault::PrintFault` and `FeatherFault::GetFault`, allowing the developer to determine if the board has failed after it resets.

### Usage Example

To show how this behavior works, let's assume that `unsafe_function()` in the code block below attempts to access memory that doesn't exist, causing a hard fault:
```C++
void setup() {
    // Activate featherfault
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

## Implementation Notes

### Failure Modes

FeatherFault currently handles three failure modes: hanging, [memory overflow](https://learn.adafruit.com/memories-of-an-arduino?view=all), and [hard fault](https://www.freertos.org/Debugging-Hard-Faults-On-Cortex-M-Microcontrollers.html). When any of these failure modes are triggered, FeatherFault will immediately write the information from the last `MARK` to flash memory, and cause a system reset. `FeatherFault::PrintFault`, `FeatherFault::GetFault`, and `FeatherFault::DidFault` read this flash memory to retrieve information regarding the last fault.

#### Hanging Detection

Hanging detection is implemented using the SAMD watchdog timer early warning interrupt. As a result, FeatherFault will not detect hanging unless `FeatherFault::StartWDT` is called somewhere in the beginning of the sketch. Note that similar to normal watchdog operation, FeatherFaults detection must be periodically resetting using `MARK` macro; this means that the `MARK` macro must be placed such that it is called at least periodically under the timeout specified. If you have a long operation that you cannot `MARK` (sleep being an example), you can use `FeatherFault::StopWDT` to disable the watchdog during that time.

#### Memory Overflow Detection

Memory overflow detection is implemented by checking the top of the heap against the top of the stack. If the stack is overwriting the heap, memory is assumed to be corrupted and the board is immediately reset. This check is performed inside the `MARK` macro.

#### Hard Fault Detection

Hard Fault detection is implemented using the existing hard fault interrupt vector built into ARM. This interrupt is normally [defined as a infinite loop](https://github.com/adafruit/ArduinoCore-samd/blob/bf24e95f7ef7b41201d4389ef47b858b14ca58dd/cores/arduino/cortex_handlers.c#L43), however FeatherFault overrides this handler to allow for tracing and a graceful recovery. This feature is activated when the library is included in your sketch.

### Getting Fault Data In Your Sketch

While most projects should only need traces on the serial monitor, some (such as remote deployments) will need to log the data to other mediums. To do this, FeatherFault has the `FeatherFault::DidFault` and `FeatherFault::GetFault` functions to check if a fault has occurred, and to get the last fault trace. For more information on these functions, please see [FeatherFault.h](./src/FeatherFault.h).

### Getting Fault Data Without Serial

If you cannot establish a serial connection with your board but you still have USB access, you can use the [recover_fault python script](./tools/recover_fault/recover_fault.py) to download and read trace data from your board. Simply follow the setup instructions contained in the script, reset the board into bootloader mode, and run:
```
python ./recover_fault.py recover <comport>
```