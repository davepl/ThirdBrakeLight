// * Copyright 2019 Dave Plummer

#include <Arduino.h>
#define FASTLED_INTERNAL 1 // Quiet the FastLED compiler banner
#include "./LEDStripGFX.h"
#include "./LightingEvents.h"
#include "./globals.h"
#include <FastLED.h> // FastLED for the LED panels
#include <array>
#include <heltec.h>
#include <pixeltypes.h> // Handy color and hue stuff

// Global brightness scalar - everthing you do is ultimately multiplied by this
// fraction of 255

constexpr byte     g_Brightness            = 255;
constexpr uint32_t DiagnosticFrameInterval = 50;

LEDStripGFX g_Strip(NUMBER_USED_PIXELS);

BrakingEvent   g_Braking(&g_Strip, 0);
BackupEvent    g_Backup(&g_Strip, BACKUP_PIN);
SignalEvent    g_LeftTurn(&g_Strip, LEFT_TURN_PIN, SignalEvent::Style::LeftTurn);
SignalEvent    g_RightTurn(&g_Strip, RIGHT_TURN_PIN, SignalEvent::Style::RightTurn);
PoliceLightBar g_Emergency(&g_Strip, EMERGENCY_PIN);

const std::array<LightingEvent*, 5> g_AllEffects = {
    &g_Emergency, &g_Braking, &g_LeftTurn, &g_RightTurn, &g_Backup,
};

// The IRQ vectors do not include accomodation for any context or data, so you
// can't pass a "this" pointer, which means each IRQ we set must go to a functon
// that then dispatches to the object in question.  It works!  IRAM_ATTR so
// they're always in RAM.

void IRAM_ATTR BrakingIRQ()
{
    g_Braking.IRQ();
}
void IRAM_ATTR BackupIRQ()
{
    g_Backup.IRQ();
}
void IRAM_ATTR LeftTurnIRQ()
{
    g_LeftTurn.IRQ();
}
void IRAM_ATTR RightTurnIRQ()
{
    g_RightTurn.IRQ();
}
void IRAM_ATTR EmergencyIRQ()
{
    g_Emergency.IRQ();
}

class UI
{
public:
    void DrawIndicators()
    {
        char line[64];
        char stats[16];

        const bool leftPressed      = IsInputPressed(LEFT_TURN_PIN);
        const bool rightPressed     = IsInputPressed(RIGHT_TURN_PIN);
        const bool backupPressed    = IsInputPressed(BACKUP_PIN);
        const bool emergencyPressed = IsInputPressed(EMERGENCY_PIN);
        const bool brakePressed     = leftPressed && rightPressed;

        snprintf(line, sizeof(line), "L%s B%s R%s Bk%s E%s", leftPressed ? "*" : ".",
                 brakePressed ? "*" : ".", rightPressed ? "*" : ".", backupPressed ? "*" : ".",
                 emergencyPressed ? "*" : ".");
        snprintf(stats, sizeof(stats), "FPS %d", FastLED.getFPS());

        Heltec.display->clear();
        Heltec.display->drawString(0, 0, line);
        Heltec.display->drawString(0, 16, stats);
        Heltec.display->display();
    }
};

UI g_UI;

void DrawBootScreen()
{
    Heltec.display->clear();
    Heltec.display->drawString(0, 0, "ThirdBrakeLight");
    Heltec.display->drawString(0, 16, "Heltec WiFi Kit 32 V3");
    Heltec.display->display();

    g_Strip.fillScreen(BLACK16);
    g_Strip.ShowStrip();

    delay(1500);
}

// displayLoop
//
// The display loop is just a thread that sits and draws the display over and
// over forever.

void displayLoop(void*)
{
    for (;;)
    {
        g_UI.DrawIndicators();
        delay(10);
    }
}

// setup
//
// Setup is called one time at chip boot, before loop(), to do... setup.  Like
// which pins are input or output, setting up interrupts, and other one-time
// things.

void setup()
{
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

    for (auto* effect : g_AllEffects)
        effect->SyncToInput();

    Serial.println("Clearing Strip...");

    // Initialize FastLED here (NOT in the LEDStripGFX global constructor).
    // Doing this before any strip draw calls ensures the ESP32-S3's RMT / I2C
    // peripherals are in a known good state when the Heltec OLED is brought up.
    g_Strip.Begin();

    g_Strip.setBrightness(255);
    g_Strip.fillScreen(BLACK16);
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

    if (xTaskCreateUniversal(displayLoop, "displayLoop", 4096, nullptr, 1, &uiTask, 0) != pdPASS)
        Serial.println("Failed to start display task.");
}

// processAndDisplayInputs()
//
// Main update loop

void processAndDisplayInputs()
{
    g_Strip.fillScreen(BLACK16);

    for (auto* effect : g_AllEffects)
        effect->CheckForButtonPress();

    const bool leftPressed  = IsInputPressed(LEFT_TURN_PIN);
    const bool rightPressed = IsInputPressed(RIGHT_TURN_PIN);

    if (leftPressed && rightPressed)
    {
        g_LeftTurn.SetActive(false);
        g_RightTurn.SetActive(false);
        g_Braking.Begin();
    }
    else if (g_Braking.GetActive())
    {
        g_Braking.End();

        if (leftPressed)
            g_LeftTurn.Begin();

        if (rightPressed)
            g_RightTurn.Begin();
    }

    for (auto* effect : g_AllEffects)
        effect->Draw();

    g_Strip.setBrightness(g_Brightness);
    g_Strip.ShowStrip();
}

// loop
//
// Called repeatedly by Arduino framework, this is the main loop of the program

void loop()
{
    static ulong frame = 0;

    frame++;
    processAndDisplayInputs();

    // Continuous-but-throttled diagnostics. We don't want to print every single
    // frame; ~20 lines/sec is plenty to follow.
    if (frame % DiagnosticFrameInterval == 0)
    {
        Serial.printf("f=%lu  L:p=%d irq=%lu act=%d  R:p=%d irq=%lu act=%d  "
                      "Bk:p=%d irq=%lu act=%d  E:p=%d irq=%lu act=%d  Br:act=%d  fps=%d\n",
                      (unsigned long)frame, digitalRead(LEFT_TURN_PIN),
                      (unsigned long)g_LeftTurn.GetIRQCount(), g_LeftTurn.GetActive(),
                      digitalRead(RIGHT_TURN_PIN), (unsigned long)g_RightTurn.GetIRQCount(),
                      g_RightTurn.GetActive(), digitalRead(BACKUP_PIN),
                      (unsigned long)g_Backup.GetIRQCount(), g_Backup.GetActive(),
                      digitalRead(EMERGENCY_PIN), (unsigned long)g_Emergency.GetIRQCount(),
                      g_Emergency.GetActive(), g_Braking.GetActive(), FastLED.getFPS());
    }
    delay(1);
}
