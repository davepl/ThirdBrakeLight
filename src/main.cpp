// * Copyright 2019 Dave Plummer

#include <Arduino.h>
#define FASTLED_INTERNAL 1 // Quiet the FastLED compiler banner
#include "./LEDStripGFX.h"
#include "./LightingEvents.h"
#include "./globals.h"
#include <FastLED.h>             // FastLED for the LED panels
#include <heltec.h>
#include <pixeltypes.h>          // Handy color and hue stuff

#ifndef LED_BUILTIN
// Set LED_BUILTIN if it is not defined by Arduino framework
#define LED_BUILTIN 4
#endif

#define NUM_LEDS NUMBER_USED_PIXELS

// 5:6:5 Color definitions

#define BLACK16 0x0000
#define BLUE16 0x001F
#define RED16 0xF800
#define GREEN16 0x07E0
#define CYAN16 0x07FF
#define MAGENTA16 0xF81F
#define YELLOW16 0xFFE0
#define WHITE16 0xFFFF

// Global brightness scalar - everthing you do is ultimately multiplied by this
// fraction of 255

const byte g_Brightness = 255;

// mapFloat
//
// Given an input value x that can range from in_min to in_max, maps return
// output value between out_min and out_max

float mapFloat(float x, float in_min, float in_max, float out_min,
               float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

LEDStripGFX g_Strip(NUM_LEDS);

BrakingEvent g_Braking(&g_Strip, 0);
BackupEvent g_Backup(&g_Strip, BACKUP_PIN);
SignalEvent g_LeftTurn(&g_Strip, LEFT_TURN_PIN,
                       SignalEvent::SIGNAL_STYLE::LEFT_TURN);
SignalEvent g_RightTurn(&g_Strip, RIGHT_TURN_PIN,
                        SignalEvent::SIGNAL_STYLE::RIGHT_TURN);
PoliceLightBar g_Emergency(&g_Strip, EMERGENCY_PIN);

LightingEvent *g_pAllEffects[] = {&g_Emergency, &g_Braking, &g_LeftTurn,
                                  &g_RightTurn, &g_Backup};

// The IRQ vectors do not include accomodation for any context or data, so you
// can't pass a "this" pointer, which means each IRQ we set must go to a functon
// that then dispatches to the object in question.  It works!  IRAM_ATTR so
// they're always in RAM.

void IRAM_ATTR BrakingIRQ() { g_Braking.IRQ(); }
void IRAM_ATTR BackupIRQ() { g_Backup.IRQ(); }
void IRAM_ATTR LeftTurnIRQ() { g_LeftTurn.IRQ(); }
void IRAM_ATTR RightTurnIRQ() { g_RightTurn.IRQ(); }
void IRAM_ATTR EmergencyIRQ() { g_Emergency.IRQ(); }

// IMPORTANT: To reduce NeoPixel burnout risk, add 1000 uF capacitor across
// pixel power leads, add 300 - 500 Ohm resistor on first pixel's data input
// and minimize distance between Arduino and first pixel.  Avoid connecting
// on a live circuit...if you must, connect GND first.

// We keep track of when each vaious feature display started so that we know how
// far we are into it's animation, etc.

unsigned long backupStartTime = 0;
unsigned long brakeStartTime = 0;
unsigned long turnStartTime = 0;

bool ButtonPressed(uint8_t pin) { return digitalRead(pin) == LOW; }

class UI {
public:
  void DrawIndicators() {
    static int pass = 0;

    char line[64];
    char counter[16];

    snprintf(line, sizeof(line), "L%s B%s R%s Bk%s E%s",
             ButtonPressed(LEFT_TURN_PIN) ? "*" : ".",
             g_Braking.GetActive() ? "*" : ".",
             ButtonPressed(RIGHT_TURN_PIN) ? "*" : ".",
             ButtonPressed(BACKUP_PIN) ? "*" : ".",
             ButtonPressed(EMERGENCY_PIN) ? "*" : ".");
    snprintf(counter, sizeof(counter), "%d", pass++);

    Heltec.display->clear();
    Heltec.display->drawString(0, 0, line);
    Heltec.display->drawString(0, 16, counter);
    Heltec.display->display();
  }
};

UI g_UI;

void DrawBootScreen() {
  Heltec.display->clear();
  Heltec.display->drawString(0, 0, "ThirdBrakeLight");
  Heltec.display->drawString(0, 16, "Heltec WiFi Kit 32 V3");
  Heltec.display->display();

  g_Strip.fillScreen(BLACK16);
  g_Strip.fillScreen(WHITE16);
  g_Strip.ShowStrip();

  delay(1500);
}

// displayLoop
//
// The display loop is just a thread that sits and draws the display over and
// over forever.

void displayLoop(void *) {
  for (;;) {
    g_UI.DrawIndicators();
    delay(10);
  }
}

// setup
//
// Setup is called one time at chip boot, before loop(), to do... setup.  Like
// which pins are input or output, setting up interrupts, and other one-time
// things.

void setup() {
  Serial.begin(115200);

  Serial.println("Dave's Garage ThirdBrakeLight Startup");
  Serial.println("-------------------------------------");

  Serial.println("Configuring Inputs...");

  // Active-LOW inputs: idle = HIGH (held by internal pull-up), asserted = LOW.
  pinMode(LEFT_TURN_PIN, INPUT_PULLUP);
  pinMode(RIGHT_TURN_PIN, INPUT_PULLUP);
  pinMode(BACKUP_PIN, INPUT_PULLUP);
  pinMode(EMERGENCY_PIN, INPUT_PULLUP);

  Serial.println("Attaching Interrupts to Inputs...");

  attachInterrupt(digitalPinToInterrupt(LEFT_TURN_PIN), LeftTurnIRQ, CHANGE);
  attachInterrupt(digitalPinToInterrupt(RIGHT_TURN_PIN), RightTurnIRQ, CHANGE);
  attachInterrupt(digitalPinToInterrupt(BACKUP_PIN), BackupIRQ, CHANGE);
  attachInterrupt(digitalPinToInterrupt(EMERGENCY_PIN), EmergencyIRQ, CHANGE);

  Serial.println("Clearing Strip...");

  // Initialize FastLED here (NOT in the LEDStripGFX global constructor).
  // Doing this before any strip draw calls ensures the ESP32-S3's RMT / I2C
  // peripherals are in a known good state when the Heltec OLED is brought up.
  g_Strip.Begin();

  g_Strip.setBrightness(255);
  for (uint16_t i = 0; i < NUM_LEDS; i++)
    g_Strip.drawPixel(i, CRGB(0, 0, 0));
  g_Strip.ShowStrip();

  Serial.println("Starting Heltec V3 OLED...");
  Heltec.begin(true, false, false);
  // The Heltec library sets a default font internally, but be explicit so we
  // never end up drawing with a null font pointer if init ordering changes.
  Heltec.display->setFont(ArialMT_Plain_10);
  Heltec.display->screenRotate(ANGLE_180_DEGREE);
  DrawBootScreen();
  Serial.println("Heltec V3 OLED initialized.");
  TaskHandle_t uiTask;
  // Bigger stack (SSD1306 framebuffer + I2C overhead) and priority 1 so the
  // task isn't starved at IDLE priority. Match NightDriverStrip's pattern.
  xTaskCreateUniversal(displayLoop, "displayLoop", 4096, nullptr, 1, &uiTask,
                       0);
}

// processAndDisplayInputs()
//
// Main update loop

void processAndDisplayInputs() {
  g_Strip.fillScreen(BLACK16);

  for (int i = 0; i < ARRAYSIZE(g_pAllEffects); i++)
    g_pAllEffects[i]->CheckForButtonPress();

  if (g_LeftTurn.GetActive() && g_RightTurn.GetActive()) {
    if (g_LeftTurn.TimeElapsedTotal() < 0.05 &&
        g_RightTurn.TimeElapsedTotal() < 0.05) {
      g_LeftTurn.SetActive(false);
      g_RightTurn.SetActive(false);
      g_Braking.Begin();
    }
  } else if (g_Braking.GetActive() && digitalRead(LEFT_TURN_PIN) == HIGH &&
             digitalRead(RIGHT_TURN_PIN) == HIGH) {
    g_Braking.End();
  }

  for (int i = 0; i < ARRAYSIZE(g_pAllEffects); i++)
    g_pAllEffects[i]->Draw();

  g_Strip.setBrightness(g_Brightness);
  g_Strip.ShowStrip();
}

// loop
//
// Called repeatedly by Arduino framework, this is the main loop of the program

void loop() {
  static ulong frame = 0;

  frame++;
  processAndDisplayInputs();

  // Continuous-but-throttled diagnostics. We don't want to print every single
  // frame; ~20 lines/sec is plenty to follow.
  if (frame % 50 == 0) {
    Serial.printf(
        "f=%lu  L:p=%d irq=%lu act=%d  R:p=%d irq=%lu act=%d  "
        "Bk:p=%d irq=%lu act=%d  E:p=%d irq=%lu act=%d  Br:act=%d  fps=%d\n",
        (unsigned long)frame,
        digitalRead(LEFT_TURN_PIN),  (unsigned long)g_LeftTurn.GetIRQCount(),
        g_LeftTurn.GetActive(),
        digitalRead(RIGHT_TURN_PIN), (unsigned long)g_RightTurn.GetIRQCount(),
        g_RightTurn.GetActive(),
        digitalRead(BACKUP_PIN),     (unsigned long)g_Backup.GetIRQCount(),
        g_Backup.GetActive(),
        digitalRead(EMERGENCY_PIN),  (unsigned long)g_Emergency.GetIRQCount(),
        g_Emergency.GetActive(),
        g_Braking.GetActive(),
        FastLED.getFPS());
  }
  delay(1);
  return;
}
