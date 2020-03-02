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
  // put your main code here, to run repeatedly:
  while(true) {
    // dangerous allocation
    char* thing = (char*)malloc(1024); MARK;
    thing[0] = 'a'; MARK;
    delay(500); MARK;
  }
}