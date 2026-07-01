#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "OTL.h"

#define BUTTON_PIN 4
#define PIXEL_PIN 5

#define SERIAL_TIMER_INTERVAL_MS 250
#define SHOW_LIVE_AFTER_SWITCH_MS 1000

#define SERIAL_DISCONNECTED_HSV 8192, 255, 255

OTLsender otlSender;
Adafruit_NeoPixel strip(OTL_NUM_RECEIVERS, PIXEL_PIN, NEO_GRB + NEO_KHZ800);
OTLled leds[OTL_NUM_RECEIVERS];
OTLswitchState setStates[OTL_NUM_RECEIVERS];
unsigned long showLiveUntil[OTL_NUM_RECEIVERS];
bool partyEnabled = false;

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  while(!otlSender.begin()) delay(500);

  strip.begin();
  strip.show();

  for(OTLmodule receiver : OTLreceivers) leds[OTLreceiverToIdx(receiver)] = OTLled(map(OTLreceiverToIdx(receiver), 0, OTL_NUM_RECEIVERS, 0, 65535));
  for(OTLmodule receiver : OTLreceivers) setStates[OTLreceiverToIdx(receiver)] = OTL_STANDBY;
  for(OTLmodule receiver : OTLreceivers) showLiveUntil[OTLreceiverToIdx(receiver)] = 0;
}

void loop() {
  static unsigned long lastSerialMessage = 0;
  if (Serial.available()) {
    lastSerialMessage = millis();

    uint8_t programId = 0;
    uint8_t previewId = 0;
    uint8_t brightness = 0;
    bool validMessage = true;

    String line = Serial.readStringUntil('\n');
    line.trim();

    char* token = strtok(line.c_str(), ";");
    if(strcmp(token, "OTLCMD") == 0) {
      int i;
      for(i = 0; (token = strtok(NULL, ";")) != nullptr; i++) {
        char *end;
        long int val = strtol(token, &end, 10);

        if(*end == '\0' && val >= 0 && val <= 255) {
          if(i == 0) programId = val;
          if(i == 1) previewId = val;
          if(i == 2) brightness = val;
        }
        else validMessage = false;
      }
      if(i < 2) validMessage = false;
    }
    else validMessage = false;

    if(validMessage) {
      for(OTLmodule receiver : OTLreceivers) {
        if(receiver == programId) {
          if(setStates[OTLreceiverToIdx(receiver)] != OTL_LIVE) {
            if(!partyEnabled) otlSender.switchState(receiver, OTL_LIVE);
            setStates[OTLreceiverToIdx(receiver)] = OTL_LIVE;
          }
        }
        else if(receiver == previewId) {
          if (setStates[OTLreceiverToIdx(receiver)] != OTL_READY) {
            if(setStates[OTLreceiverToIdx(receiver)] == OTL_LIVE) showLiveUntil[OTLreceiverToIdx(receiver)] = millis() + SHOW_LIVE_AFTER_SWITCH_MS;
            if(showLiveUntil[OTLreceiverToIdx(receiver)] == 0 && !partyEnabled) otlSender.switchState(receiver, OTL_READY);
            setStates[OTLreceiverToIdx(receiver)] = OTL_READY;
          }
        }
        else {
          if (setStates[OTLreceiverToIdx(receiver)] != OTL_STANDBY) {
            if(setStates[OTLreceiverToIdx(receiver)] == OTL_LIVE) showLiveUntil[OTLreceiverToIdx(receiver)] = millis() + SHOW_LIVE_AFTER_SWITCH_MS;
            if(showLiveUntil[OTLreceiverToIdx(receiver)] == 0 && !partyEnabled) otlSender.switchState(receiver, OTL_STANDBY);
            setStates[OTLreceiverToIdx(receiver)] = OTL_STANDBY;
          }
        }
      }
      otlSender.setBrightness(brightness);
    }
  }

  for(OTLmodule receiver : OTLreceivers) {
    if(showLiveUntil[OTLreceiverToIdx(receiver)] != 0 && showLiveUntil[OTLreceiverToIdx(receiver)] <= millis()) {
      otlSender.switchState(receiver, setStates[OTLreceiverToIdx(receiver)]);
      showLiveUntil[OTLreceiverToIdx(receiver)] = 0;
    }
  }

  bool serialConnected = lastSerialMessage + 2 * SERIAL_TIMER_INTERVAL_MS >= millis();
  if(!serialConnected && !partyEnabled) {
    for(OTLmodule receiver : OTLreceivers) {
      if (setStates[OTLreceiverToIdx(receiver)] != OTL_STANDBY) {
        otlSender.switchState(receiver, OTL_STANDBY);
        setStates[OTLreceiverToIdx(receiver)] = OTL_STANDBY;
      }
    }
  }

  static unsigned long lastButtonChange = 0;
  static int lastButtonState = HIGH;
  int buttonState = digitalRead(BUTTON_PIN);
  if(millis() > lastButtonChange + 50 && buttonState != lastButtonState) {
    if(buttonState == LOW) {
      partyEnabled = !partyEnabled;
      if(partyEnabled) for(OTLmodule receiver : OTLreceivers) otlSender.switchState(receiver, OTL_PARTY);
      else for(OTLmodule receiver : OTLreceivers) otlSender.switchState(receiver, setStates[OTLreceiverToIdx(receiver)]);
    }
    lastButtonChange = millis();
    lastButtonState = buttonState;
  }

  otlSender.update();
  for(OTLmodule receiver : OTLreceivers) leds[OTLreceiverToIdx(receiver)].update(otlSender.getState(receiver), otlSender.getDisabled(receiver));

  strip.setBrightness(otlSender.getBrightness());
  for(OTLmodule receiver : OTLreceivers) {
    OTLhsv color = leds[OTLreceiverToIdx(receiver)].getHSV();
    if(serialConnected || partyEnabled) strip.setPixelColor(OTLreceiverToIdx(receiver), strip.gamma32(strip.ColorHSV(color.h, color.s, color.v)));
    else strip.setPixelColor(OTLreceiverToIdx(receiver), strip.gamma32(strip.ColorHSV(SERIAL_DISCONNECTED_HSV)));
  }
  strip.show();
}