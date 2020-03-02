#include "FeatherFault.h"

void setup() {
  digitalWrite(13, LOW);
  delay(100);
  digitalWrite(13, HIGH);
  // put your setup code here, to run once:
  Serial.begin(115200);
  while(!Serial);
  FeatherFault::PrintFault(Serial);
  Serial.flush();
  FeatherFault::StartWDT(FeatherFault::WDTTimeout::WDT_8S); MARK;
}

void loop() {
  Serial.println("start!"); MARK;
  Serial.flush(); MARK;
  delay(1000); MARK;
  // cause a hardfault
  __builtin_trap(); MARK;
}