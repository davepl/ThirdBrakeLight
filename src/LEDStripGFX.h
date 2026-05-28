//+--------------------------------------------------------------------------
//
// ThirdBrakeLight - (c) 2018 Dave Plummer.  All Rights Reserved.
//
// File:        LEDStripGFX.h
//
// Description:
//
//   Provides a Adafruit_GFX implementation for our RGB LED panel so that
//   we can use primitives such as lines and fills on it.
//
// History:     Oct-9-2018         Davepl      Created from other projects
//              Jun-30-2019        Davepl      Adapted from LEDStrip projects
//
//---------------------------------------------------------------------------

#pragma once
#define FASTLED_INTERNAL 1
#include "Adafruit_GFX.h"
#include "FastLED.h"
#include "globals.h"
#include "pixeltypes.h"
#include <array>

class LEDStripGFX : public Adafruit_GFX
{
private:
    std::array<CRGB, NUMBER_USED_PIXELS> _leds{};
    size_t                               _width;

    bool Contains(int16_t x, int16_t y) const
    {
        return x >= 0 && y >= 0 && static_cast<size_t>(x) < _width &&
               static_cast<size_t>(y) < MATRIX_HEIGHT;
    }

public:
    explicit LEDStripGFX(size_t width)
        : Adafruit_GFX(static_cast<int16_t>(width), MATRIX_HEIGHT),
          _width(width <= _leds.size() ? width : _leds.size())
    {
    }

    // Call from setup() AFTER the Arduino framework is up.
    void Begin()
    {
        FastLED.addLeds<WS2812B, LED_PIN, GRB>(_leds.data(), _width);
        FastLED.setBrightness(255);
    }

    void ShowStrip() { FastLED.show(); }

    void setBrightness(byte brightness) { FastLED.setBrightness(brightness); }

    CRGB* GetLEDBuffer() { return _leds.data(); }

    size_t GetLEDCount() const { return _width; }

    static const byte gamma5[];
    static const byte gamma6[];

    inline static CRGB from16Bit(
        uint16_t color) // Convert 16bit 5:6:5 to 24bit color using lookup table for gamma
    {
        byte r = gamma5[color >> 11];
        byte g = gamma6[(color >> 5) & 0x3F];
        byte b = gamma5[color & 0x1F];

        return CRGB(r, g, b);
    }

    static inline uint16_t to16bit(uint8_t r, uint8_t g, uint8_t b) // Convert RGB -> 16bit 5:6:5
    {
        return ((r / 8) << 11) | ((g / 4) << 5) | (b / 8);
    }

    static inline uint16_t to16bit(const CRGB rgb) // Convert CRGB -> 16 bit 5:6:5
    {
        return ((rgb.r / 8) << 11) | ((rgb.g / 4) << 5) | (rgb.b / 8);
    }

    static inline uint16_t to16bit(
        CRGB::HTMLColorCode code) // Convert HtmlColorCode -> 16 bit 5:6:5
    {
        return to16bit(CRGB(code));
    }

    size_t getPixelIndex(int16_t x, int16_t y) const
    {
        if (x & 0x01)
        {
            // Odd rows run backwards
            const size_t reverseY = (MATRIX_HEIGHT - 1) - y;
            return (static_cast<size_t>(x) * MATRIX_HEIGHT) + reverseY;
        }

        // Even rows run forwards.
        return (static_cast<size_t>(x) * MATRIX_HEIGHT) + y;
    }

    CRGB getPixel(int16_t x, int16_t y) const
    {
        if (!Contains(x, y))
            return CRGB::Black;

        return _leds[getPixelIndex(x, y)];
    }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override
    {
        if (Contains(x, y))
            _leds[getPixelIndex(x, y)] = from16Bit(color);
    }

    void drawPixel(int16_t x, int16_t y, CRGB color)
    {
        if (Contains(x, y))
            _leds[getPixelIndex(x, y)] = color;
    }

    void drawPixel(size_t x, CRGB color)
    {
        if (x < _width)
            _leds[x] = color;
    }
};
