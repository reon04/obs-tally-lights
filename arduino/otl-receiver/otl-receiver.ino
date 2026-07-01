#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "OTL.h"

#define BUTTON_PIN 4
#define PIXEL_PIN 5
#define PIXEL_COUNT 6

OTLreceiver otlReceiver(OTL_MODULE_RECEIVER_1);
Adafruit_NeoPixel strip(PIXEL_COUNT, PIXEL_PIN, NEO_GRB + NEO_KHZ800);
OTLled leds[PIXEL_COUNT];

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  while(!otlReceiver.begin()) delay(500);

  strip.begin();
  strip.show();

  for(int i = 0; i < PIXEL_COUNT; i++) leds[i] = OTLled(map(i, 0, PIXEL_COUNT, 0, 65535));
}

void loop() {
  static unsigned long lastButtonChange = 0;
  static int lastButtonState = HIGH;
  int buttonState = digitalRead(BUTTON_PIN);
  if(millis() > lastButtonChange + 50 && buttonState != lastButtonState) {
    if(buttonState == LOW) otlReceiver.switchDisabled(!otlReceiver.getDisabled());
    lastButtonChange = millis();
    lastButtonState = buttonState;
  }

  otlReceiver.update();
  for(int i = 0; i < PIXEL_COUNT; i++) leds[i].update(otlReceiver.getState(), otlReceiver.getDisabled());

  strip.setBrightness(otlReceiver.getBrightness());
  for(int i = 0; i < PIXEL_COUNT; i++) {
    OTLhsv color = leds[i].getHSV();
    strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(color.h, color.s, color.v)));
  }
  strip.show();
}