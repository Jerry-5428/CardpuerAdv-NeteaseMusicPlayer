#include <Arduino.h>

#include "Log.h"
#include "app/App.h"

static App g_app;

void setup() {
  Serial.begin(115200);
  delay(200);
  LOGI("MAIN", "Cardputer-Adv NetEase Music Player booting");
  g_app.begin();
}

void loop() { g_app.loop(); }
