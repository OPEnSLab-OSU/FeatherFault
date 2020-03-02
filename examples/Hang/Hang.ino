#include "FeatherFault.h"

void setup() {
  digitalWrite(13, LOW);
  delay(100);
  digitalWrite(13, HIGH);
  // put your setup code here, to run once:
  Serial.begin(115200);
  while(!Serial);
  FeatherFault::StartWDT(FeatherFault::WDTTimeout::WDT_8S); MARK;
  FeatherFault::PrintFault(Serial); MARK;
  Serial.flush(); MARK;
}

void loop() {
  Serial.println("start!"); MARK;
  // Uh oh! we wrote bad code that's going to hang!
  while(true);
}