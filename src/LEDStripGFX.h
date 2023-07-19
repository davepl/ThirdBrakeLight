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
#include "globals.h"
#include "FastLED.h"
#include "Adafruit_GFX.h"
#include "pixeltypes.h"

// 5:6:5 Color definitions
#define BLACK16 0x0000
#define BLUE16 0x001F
#define RED16 0xF800
#define GREEN16 0x07E0
#define CYAN16 0x07FF
#define MAGENTA16 0xF81F
#define YELLOW16 0xFFE0
#define WHITE16 0xFFFF

//     FastLED.addLeds<WS2812B, LED_PIN, GRB>(_pLEDs, w * h);


class LEDStripGFX : public Adafruit_GFX
{
  friend class LEDStripEffect;                                          // I might a shower after this lifetime first, but it needs acess to the pixels to do its job, so BUGBUG expose this more nicely

private:
  CRGB *_pLEDs;
  size_t _width;

public:
  LEDStripGFX(size_t w)
      : Adafruit_GFX((int)w, 1),
        _width(w)
  {
    _pLEDs = static_cast<CRGB *>(calloc(w , sizeof(CRGB)));
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(_pLEDs, w);
    FastLED.setBrightness(127);    
  }

  void ShowStrip()
  {
      FastLED.show();    
  }

  void setBrightness(byte brightness)
  {
    FastLED.setBrightness(brightness);
  }

  ~LEDStripGFX()
  {
    free(_pLEDs);
    _pLEDs = nullptr;
  }

  CRGB * GetLEDBuffer()
  {
    return _pLEDs;
  }

  size_t GetLEDCount()
  {
    return _width;
  }

  static const byte gamma5[];
  static const byte gamma6[];

  inline static CRGB from16Bit(uint16_t color) // Convert 16bit 5:6:5 to 24bit color using lookup table for gamma
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

  static inline uint16_t to16bit(CRGB::HTMLColorCode code) // Convert HtmlColorCode -> 16 bit 5:6:5
  {
    return to16bit(CRGB(code));
  }

  inline uint16_t getPixelIndex(int16_t x, int16_t y) const
  {
    if (x & 0x01)
    {
      // Odd rows run backwards
      uint8_t reverseY = (_height - 1) - y;
      return (x * _height) + reverseY;
    }
    else
    {
      // Even rows run forwards
      return (x * _height) + y;
    }
  }

  inline CRGB getPixel(int16_t x, int16_t y) const
  {
    return _pLEDs[getPixelIndex(x, y)];
  }

  inline virtual void drawPixel(int16_t x, int16_t y, uint16_t color)
  {
    if (x >= 0 && x <= MATRIX_WIDTH && y >= 0 && y <= MATRIX_HEIGHT)
      _pLEDs[getPixelIndex(x, y)] = from16Bit(color);
  }

  inline virtual void drawPixel(int16_t x, int16_t y, CRGB color)
  {
    if (x >= 0 && x <= MATRIX_WIDTH && y >= 0 && y <= MATRIX_HEIGHT)
      _pLEDs[getPixelIndex(x, y)] = color;
  }

  inline virtual void drawPixel(int x, CRGB color)
  {
    if (x >= 0 && x <= MATRIX_WIDTH * MATRIX_HEIGHT)
      _pLEDs[x] = color;
  }
};
