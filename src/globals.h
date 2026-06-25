
#pragma once
#include <Arduino.h>

inline constexpr uint16_t MATRIX_WIDTH  = (144+80);
inline constexpr uint16_t MATRIX_HEIGHT = 1;

inline constexpr uint8_t LED_PIN = 5;

inline constexpr uint8_t LEFT_TURN_PIN  = 2;
inline constexpr uint8_t RIGHT_TURN_PIN = 4;
inline constexpr uint8_t BACKUP_PIN     = 6;
inline constexpr uint8_t EMERGENCY_PIN  = 7;
inline constexpr uint8_t DEMO_PIN       = 0;   // Heltec V3 on-board PRG (BOOT) button

// Sentinel for "no pin assigned" - real GPIO 0 is the PRG button on Heltec V3,
// so we cannot use 0 as the unused-pin sentinel.
inline constexpr uint8_t PIN_NONE = 0xFF;

inline constexpr uint16_t NUMBER_USED_PIXELS = MATRIX_WIDTH * MATRIX_HEIGHT;
inline constexpr uint16_t NUMBER_TURN_PIXELS = 87;

inline constexpr uint16_t BLACK16 = 0x0000;

inline bool IsInputPressed(uint8_t pin)
{
    return digitalRead(pin) == LOW;
}
