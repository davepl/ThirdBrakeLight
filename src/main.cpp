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

// Set to 0 while debugging - light sleep will drop the USB-CDC serial monitor.
#define ENABLE_SLEEP 1

#if ENABLE_SLEEP
#include <driver/gpio.h>
#include <esp_sleep.h>
constexpr uint32_t IDLE_SLEEP_MS = 30000;
#endif

// Global brightness scalar - everthing you do is ultimately multiplied by this
// fraction of 255

constexpr byte     g_Brightness            = 255;
constexpr uint32_t DiagnosticFrameInterval = 50;
constexpr float    BrakeDetectionWindow    = 0.05f;

LEDStripGFX g_Strip(NUMBER_USED_PIXELS);

BrakingEvent   g_Braking(&g_Strip, PIN_NONE);
BackupEvent    g_Backup(&g_Strip, BACKUP_PIN);
SignalEvent    g_LeftTurn(&g_Strip, LEFT_TURN_PIN, SignalEvent::Style::LeftTurn);
SignalEvent    g_RightTurn(&g_Strip, RIGHT_TURN_PIN, SignalEvent::Style::RightTurn);
PoliceLightBar g_Emergency(&g_Strip, EMERGENCY_PIN);

const std::array<LightingEvent*, 5> g_AllEffects = 
{
    &g_Emergency, &g_Braking, &g_LeftTurn, &g_RightTurn, &g_Backup,
};

// The IRQ vectors do not include accomodation for any context or data, so you
// can't pass a "this" pointer, which means each IRQ we set must go to a function
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

// Demo-mode button (Heltec V3 PRG / GPIO 0). Press once to start a 5-second
// cycle through every effect; press again to stop. Separate from the
// LightingEvent machinery since it's not an effect itself.

volatile bool g_DemoButtonPressed = false;
bool          g_DemoMode          = false;

void IRAM_ATTR DemoIRQ()
{
    static uint32_t lastMs = 0;
    uint32_t        now    = millis();
    if (now - lastMs < 200)
        return;
    if (digitalRead(DEMO_PIN) == LOW)
    {
        g_DemoButtonPressed = true;
        lastMs              = now;
    }
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
    pinMode(DEMO_PIN, INPUT_PULLUP);

    Serial.println("Attaching Interrupts to Inputs...");

    attachInterrupt(digitalPinToInterrupt(LEFT_TURN_PIN), LeftTurnIRQ, CHANGE);
    attachInterrupt(digitalPinToInterrupt(RIGHT_TURN_PIN), RightTurnIRQ, CHANGE);
    attachInterrupt(digitalPinToInterrupt(BACKUP_PIN), BackupIRQ, CHANGE);
    attachInterrupt(digitalPinToInterrupt(EMERGENCY_PIN), EmergencyIRQ, CHANGE);
    attachInterrupt(digitalPinToInterrupt(DEMO_PIN), DemoIRQ, CHANGE);

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

    if (!g_DemoMode)
    {
        for (auto* effect : g_AllEffects)
            effect->CheckForButtonPress();

        const bool leftPressed  = IsInputPressed(LEFT_TURN_PIN);
        const bool rightPressed = IsInputPressed(RIGHT_TURN_PIN);

        if (leftPressed && rightPressed && g_LeftTurn.GetActive() && g_RightTurn.GetActive() &&
            g_LeftTurn.TimeElapsedTotal() < BrakeDetectionWindow &&
            g_RightTurn.TimeElapsedTotal() < BrakeDetectionWindow)
        {
            g_LeftTurn.SetActive(false);
            g_RightTurn.SetActive(false);
            g_Braking.Begin();
        }
        else if (g_Braking.GetActive() && !leftPressed && !rightPressed)
        {
            g_Braking.End();
        }
    }

    for (auto* effect : g_AllEffects)
        effect->Draw();

    g_Strip.setBrightness(g_Brightness);
    g_Strip.ShowStrip();
}

// -------- Demo mode ---------------------------------------------------------
//
// PRG button toggles a self-running demo that cycles through every effect on
// a fixed interval so you can validate the hardware without wiring anything
// to the input pins.

constexpr uint32_t DEMO_STEP_MS = 5000;

enum class DemoStep : int
{
    Left = 0,
    Right,
    Brake,
    Hazard,
    Emergency,
    Backup,
    Count
};

uint32_t g_DemoStartMs  = 0;
int      g_DemoLastStep = -1;

static void StopAllEffects()
{
    for (auto* effect : g_AllEffects)
        effect->SetActive(false);
}

static void ApplyDemoStep(int step)
{
    StopAllEffects();
    switch (static_cast<DemoStep>(step))
    {
        case DemoStep::Left:      g_LeftTurn.Begin();  break;
        case DemoStep::Right:     g_RightTurn.Begin(); break;
        case DemoStep::Brake:     g_Braking.Begin();   break;
        case DemoStep::Hazard:    g_LeftTurn.Begin(); g_RightTurn.Begin(); break;
        case DemoStep::Emergency: g_Emergency.Begin(); break;
        case DemoStep::Backup:    g_Backup.Begin();    break;
        default: break;
    }
}

static const char* DemoStepName(int step)
{
    switch (static_cast<DemoStep>(step))
    {
        case DemoStep::Left:      return "Left";
        case DemoStep::Right:     return "Right";
        case DemoStep::Brake:     return "Brake";
        case DemoStep::Hazard:    return "Hazard";
        case DemoStep::Emergency: return "Emergency";
        case DemoStep::Backup:    return "Backup";
        default:                  return "?";
    }
}

static void ServiceDemo()
{
    if (g_DemoButtonPressed)
    {
        g_DemoButtonPressed = false;
        g_DemoMode          = !g_DemoMode;
        StopAllEffects();
        if (g_DemoMode)
        {
            Serial.println("DEMO mode: ON");
            g_DemoStartMs  = millis();
            g_DemoLastStep = -1;
        }
        else
        {
            Serial.println("DEMO mode: OFF");
        }
    }

    if (!g_DemoMode)
        return;

    const int step = ((millis() - g_DemoStartMs) / DEMO_STEP_MS) %
                     static_cast<int>(DemoStep::Count);
    if (step != g_DemoLastStep)
    {
        g_DemoLastStep = step;
        ApplyDemoStep(step);
        Serial.printf("DEMO step: %s\n", DemoStepName(step));
    }
}

#if ENABLE_SLEEP

// Sum of IRQ counts across all inputs. Changes whenever any pin toggles, so
// we can detect activity without caring which pin moved.
static uint32_t TotalIRQCount()
{
    return g_LeftTurn.GetIRQCount() + g_RightTurn.GetIRQCount() + g_Backup.GetIRQCount() +
           g_Emergency.GetIRQCount();
}

static bool AnyEffectActive()
{
    for (auto* effect : g_AllEffects)
        if (effect->GetActive())
            return true;
    return false;
}

// Enter light sleep until any input goes LOW (asserted). Light sleep on the
// ESP32-S3 preserves RAM and resumes execution right after this call - wake
// latency is ~1-3 ms, fast enough that brake response is imperceptible.
static void EnterLightSleep()
{
    Serial.println("Sleeping...");
    Serial.flush();

    // Blank the strip and OLED so neither draws power while we're idle.
    g_Strip.fillScreen(BLACK16);
    g_Strip.ShowStrip();
    Heltec.display->displayOff();

    // Wake on any input going LOW (active-LOW assertion). Idle state is HIGH
    // via INPUT_PULLUP, so we should not wake spuriously.
    gpio_wakeup_enable((gpio_num_t)LEFT_TURN_PIN, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)RIGHT_TURN_PIN, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)BACKUP_PIN, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)EMERGENCY_PIN, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)DEMO_PIN, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    esp_light_sleep_start();

    // ---- woke up here ----
    Heltec.display->displayOn();
    Serial.println("Waking...");

    // Re-read live pin levels so any edge that happened while asleep is
    // reflected in event state on the very next loop iteration.
    for (auto* effect : g_AllEffects)
        effect->SyncToInput();
}

#endif // ENABLE_SLEEP

// loop
//
// Called repeatedly by Arduino framework, this is the main loop of the program

void loop()
{
    static ulong frame = 0;

    frame++;
    ServiceDemo();
    processAndDisplayInputs();

#if ENABLE_SLEEP
    // Idle detector: sleep only when no IRQ has fired for IDLE_SLEEP_MS AND no
    // effect is currently running (a held brake produces no edges but must
    // stay lit, so we can't rely on IRQ activity alone).
    static uint32_t lastActivityMs = 0;
    static uint32_t lastIRQTotal   = 0;
    const uint32_t  now            = millis();
    const uint32_t  irqTotal       = TotalIRQCount();

    if (irqTotal != lastIRQTotal || AnyEffectActive())
    {
        lastIRQTotal   = irqTotal;
        lastActivityMs = now;
    }
    else if (now - lastActivityMs > IDLE_SLEEP_MS)
    {
        EnterLightSleep();
        lastActivityMs = millis();
        lastIRQTotal   = TotalIRQCount();
    }
#endif

    // Continuous-but-throttled diagnostics. We don't want to print every single
    // frame; ~20 lines/sec is plenty to follow.
    if (frame % DiagnosticFrameInterval == 0)
    {
        Serial.printf("f=%lu  L:p=%d irq=%lu act=%d  R:p=%d irq=%lu act=%d  "
                      "Bk:p=%d irq=%lu act=%d  E:p=%d irq=%lu act=%d  "
                      "Br:act=%d  fps=%d\n",
                      (unsigned long)frame, digitalRead(LEFT_TURN_PIN),
                      (unsigned long)g_LeftTurn.GetIRQCount(), g_LeftTurn.GetActive(),
                      digitalRead(RIGHT_TURN_PIN), (unsigned long)g_RightTurn.GetIRQCount(),
                      g_RightTurn.GetActive(), digitalRead(BACKUP_PIN),
                      (unsigned long)g_Backup.GetIRQCount(), g_Backup.GetActive(),
                      digitalRead(EMERGENCY_PIN), (unsigned long)g_Emergency.GetIRQCount(),
                      g_Emergency.GetActive(), g_Braking.GetActive(),
                      FastLED.getFPS());
    }
    delay(1);
}
