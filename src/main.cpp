// * Copyright 2019 Dave Plummer

#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>              // So we can talk to the CUU text
#define FASTLED_INTERNAL 1        // Quiet the FastLED compiler banner
#include <FastLED.h>              // FastLED for the LED panels
#include <pixeltypes.h>           // Handy color and hue stuff
#include <gfxfont.h>              // Adafruit GFX for the panels
#include <Fonts/FreeSans9pt7b.h>  // A nice font for the VFD
#include <Adafruit_GFX.h>         // GFX wrapper so we can draw on matrix
#include "./globals.h"
#include "./wrover_kit_lcd.h"
#include "./LEDStripGFX.h"
#include "./LightingEvents.h"

#if USE_TFT
	#if SMALL_TFT
		U8G2_SSD1306_128X64_NONAME_F_SW_I2C g_TFT(U8G2_R2, 15, 4, 16);
		//U8G2_SSD1306_128X64_NONAME_F_HW_I2C g_TFT(U8G2_R2, /*reset*/ 16, /*clk*/ 15, /*data*/ 4);

	#else
		WROVER_KIT_g_TFT g_TFT;
	#endif
#endif

#ifndef LED_BUILTIN
// Set LED_BUILTIN if it is not defined by Arduino framework
#define LED_BUILTIN 4
#endif

#define NUM_LEDS NUMBER_USED_PIXELS

// 5:6:5 Color definitions

#define BLACK16   0x0000
#define BLUE16    0x001F
#define RED16     0xF800
#define GREEN16   0x07E0
#define CYAN16    0x07FF
#define MAGENTA16 0xF81F
#define YELLOW16  0xFFE0
#define WHITE16   0xFFFF

// Global brightness scalar - everthing you do is ultimately multiplied by this fraction of 255

const byte g_Brightness = 24;

// mapFloat
//
// Given an input value x that can range from in_min to in_max, maps return output value between out_min and out_max

float mapFloat(float x, float in_min, float in_max, float out_min, float out_max)
{
	return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

LEDStripGFX 	g_Strip(NUM_LEDS);

BrakingEvent 	g_Braking(&g_Strip,   0);
BackupEvent  	g_Backup(&g_Strip,    BACKUP_PIN);
SignalEvent  	g_LeftTurn(&g_Strip,  LEFT_TURN_PIN, 	SignalEvent::SIGNAL_STYLE::LEFT_TURN);
SignalEvent  	g_RightTurn(&g_Strip, RIGHT_TURN_PIN,   SignalEvent::SIGNAL_STYLE::RIGHT_TURN);
PoliceLightBar 	g_Emergency(&g_Strip, EMERGENCY_PIN);

LightingEvent *	g_pAllEffects[] = {  &g_Emergency, &g_Braking, &g_LeftTurn, &g_RightTurn, &g_Backup };

// The IRQ vectors do not include accomodation for any context or data, so you can't pass a "this" pointer, which means each
// IRQ we set must go to a functon that then dispatches to the object in question.  It works!  IRAM_ATTR so they're always in RAM.

void IRAM_ATTR BrakingIRQ()		{	g_Braking.IRQ();	}
void IRAM_ATTR BackupIRQ()		{	g_Backup.IRQ();		}
void IRAM_ATTR LeftTurnIRQ()	{	g_LeftTurn.IRQ();	}
void IRAM_ATTR RightTurnIRQ()	{	g_RightTurn.IRQ();	}
void IRAM_ATTR EmergencyIRQ()	{	g_Emergency.IRQ();	}

// IMPORTANT: To reduce NeoPixel burnout risk, add 1000 uF capacitor across
// pixel power leads, add 300 - 500 Ohm resistor on first pixel's data input
// and minimize distance between Arduino and first pixel.  Avoid connecting
// on a live circuit...if you must, connect GND first.

// We keep track of when each vaious feature display started so that we know how
// far we are into it's animation, etc.

unsigned long backupStartTime = 0;
unsigned long brakeStartTime  = 0;
unsigned long turnStartTime   = 0;

#if USE_TFT
	#if SMALL_TFT
		class UI
		{
			public:

			void DrawIndicators()
			{
				g_TFT.clearBuffer();						                // Clear the internal memory
				g_TFT.setFont(u8g2_font_profont15_tf);          // Choose a suitable font 9px tall
				g_TFT.setCursor(0,10);
				g_TFT.printf("L%s B%s R%s Bk%s E%s", 
					g_LeftTurn.GetActive()  ? "*" : ".",
					g_Braking.GetActive()   ? "*" : ".",
					g_RightTurn.GetActive() ? "*" : ".",
					g_Backup.GetActive()    ? "*" : ".",
					g_Emergency.GetActive() ? "*" : ".");
				g_TFT.sendBuffer();
			}
		};

	#else
		class UI
		{
		protected:

			void DrawIndicator(int16_t x, int16_t y, CRGB color)
			{
			g_TFT.drawCircle(x, y, 10, ILI9341_LIGHTGREY);
			g_TFT.fillCircle(x, y, 8, g_TFT.color565(color.r, color.g, color.b));
			}
		
		public:

			void DrawIndicators()
			{
			g_TFT.setCursor(0, 15);
			g_TFT.print("    Left    Stop     Right   Back\n\n\n");
			DrawIndicator(g_TFT, 30, 40, g_LeftTurn.GetActive() ? CRGB::Green : CRGB::Black);
			DrawIndicator(g_TFT, 90, 40, g_Braking.GetActive() ? CRGB::Red : CRGB::Black);
			DrawIndicator(g_TFT, 150, 40, g_RightTurn.GetActive() ? CRGB::Green : CRGB::Black);
			DrawIndicator(g_TFT, 210, 40, g_Backup.GetActive() ? CRGB::Grey : CRGB::Black);

			g_TFT.print("                  Emergency\n\n\n");
			DrawIndicator(g_TFT, 150, 100, g_Emergency.GetActive() ? CRGB::Red : CRGB::Black);
 
			g_TFT.fillRect(0, 130, 200, 20, tft.color565(12, 13, 62));

			g_TFT.printf("FPS: %d", FastLED.getFPS());
			}
		};	
	#endif
#endif

#if USE_TFT
UI g_UI;

// displayLoop
//
// The display loop is just a thread that sits and draws the display over and over forever.

void displayLoop(void *)
{
	g_TFT.clear();

	for (;;)
	{
		g_UI.DrawIndicators();
		delay(10);
	}
}
#endif

// setup
//
// Setup is called one time at chip boot, before loop(), to do... setup.  Like which
// pins are input or output, setting up interrupts, and other one-time things.

void setup()
{
	Serial.begin(115200);
	Serial.println("Dave's Garage ThirdBrakeLight Startup");
	Serial.println("-------------------------------------");

	Serial.println("Configuring Inputs...");

	pinMode(LEFT_TURN_PIN,  INPUT_PULLDOWN);	
	pinMode(RIGHT_TURN_PIN, INPUT_PULLDOWN);
	pinMode(BACKUP_PIN,     INPUT_PULLDOWN);
	pinMode(EMERGENCY_PIN,  INPUT_PULLDOWN);

	Serial.println("Attaching Interrupts to Inputs...");

	attachInterrupt(digitalPinToInterrupt(LEFT_TURN_PIN),  LeftTurnIRQ,  CHANGE);
	attachInterrupt(digitalPinToInterrupt(RIGHT_TURN_PIN), RightTurnIRQ, CHANGE);
	attachInterrupt(digitalPinToInterrupt(BACKUP_PIN),     BackupIRQ,    CHANGE);
	attachInterrupt(digitalPinToInterrupt(EMERGENCY_PIN),  EmergencyIRQ, CHANGE);

	Serial.println("Clearing Strip...");

	g_Strip.setBrightness(16);
	for (uint16_t i = 0; i < NUM_LEDS; i++)
		g_Strip.drawPixel(i, CRGB(0, 0, 0));
	g_Strip.ShowStrip();

	#if USE_TFT
		#if SMALL_TFT
			g_TFT.begin();
			g_TFT.clear();
		#else
			g_TFT.begin();
			g_TFT.fillScreen(tft.color565(12, 13, 62));
			g_TFT.setFont(&FreeSans9pt7b);
		#endif
		TaskHandle_t uiTask;
		xTaskCreateUniversal(displayLoop, "displayLoop", 2048, nullptr, 0, &uiTask, 0);
	#endif
}

// processAndDisplayInputs()
// 
// Main update loop

void processAndDisplayInputs()
{
	g_Strip.fillScreen(BLACK16);

	for (int i = 0; i < ARRAYSIZE(g_pAllEffects); i++)
		g_pAllEffects[i]->CheckForButtonPress();

	if (g_LeftTurn.GetActive() && g_RightTurn.GetActive())
	{
		if (g_LeftTurn.TimeElapsedTotal() < 0.05 && g_RightTurn.TimeElapsedTotal() < 0.05)
		{
			g_LeftTurn.SetActive(false);
			g_RightTurn.SetActive(false);
			g_Braking.Begin();
		}
	}
	else if (g_Braking.GetActive() && digitalRead(LEFT_TURN_PIN) == LOW && digitalRead(RIGHT_TURN_PIN) == LOW)
	{
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

void loop()
{
	static ulong frame = 0;
	
	frame++;
	processAndDisplayInputs();

	if (frame % 1000 == 0)
	{
		Serial.printf("Speed: %d fps\n", FastLED.getFPS());
	}
	delay(1);
	return;
}