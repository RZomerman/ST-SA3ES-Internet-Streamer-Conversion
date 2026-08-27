#include <Arduino.h>
#include "rds_input.h"

void setup() {
  Serial.begin(115200);
  rds::begin();
}

void loop() {
  rds::process();
}
