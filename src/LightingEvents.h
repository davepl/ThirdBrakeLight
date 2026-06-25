//+--------------------------------------------------------------------------
//
// ThirdBrakeLight - (c) 2019 Dave Plummer.  All Rights Reserved.
//                   Offroad use only.  Use at own risk, no liability assumed.
//
// File: LightingEvents.h
//
// Description:
//
//   Implements each of the effects such as turning, braking, backup, etc.
//
// History:     Sat Aug 17 2019		Davepl		Created
//              May-27-2026         Davepl      Adapted for Lincoln, Cleanup
//
// BUGS!  Not for use on public roadways.  For one thing, I'm pretty sure
//        the signal would need to come on immediately rather than emulate
//        the incandescent fadein as it does now.  There are many FMVSS and
//        other federal, state and local ordinances you would need to worry
//        about before using this on a vehicle on the street.
//
//---------------------------------------------------------------------------

#pragma once
#include <LEDStripGFX.h>
#include <array>

inline constexpr uint32_t AMBERHI = 0xFF5000;
inline constexpr uint32_t AMBER1  = 0xFF3000;
inline constexpr uint32_t AMBER2  = 0x801800;
inline constexpr uint32_t AMBER3  = 0x400C00;
inline constexpr uint32_t AMBER4  = 0x200600;
inline constexpr uint32_t AMBER5  = 0x100300;

// We define a palette with a black background and the amber sweep in the middle,
// which will give us the effect of the signal "blooming" off and on.  The
// higher in the table that you define your colors, the earlier they will
// appear, which is safer.  This table is actually tuned to about the bloom rate
// of my 1157 signals, which wouldn't be legal for LEDs.  Pretty sure they need
// to come on "all in" and then animate from there, based on what I see Audi and
// others doing, anyway.

inline const TProgmemRGBPalette16 SignalColors_p FL_PROGMEM = 
{
    CRGB::Black, CRGB::Black, CRGB::Black, CRGB::Black, AMBERHI,
    AMBER1,      AMBER1,      AMBER2,      AMBER3,      AMBER4,
    AMBER5,      CRGB::Black, CRGB::Black, CRGB::Black, CRGB::Black,

};

inline CRGBPalette256 SignalColors_pal = SignalColors_p;

// LightingEvent (eg: BrakingEvent, SignalEvent, etc)
//
// Base class for things like turn signals, braking, backing up, etc.
// All derived classes must implement the pure virtual Draw() function.
// All derived methods MUST call their base class implementations properly
// in order for the timers to work, etc.
//
// In short, you create a derived class and then call Begin() when the
// event starts (like braking), and Update keeps track of the current
// state.  Draw() actually renders the current state of the effect to
// the light strip.

class LightingEvent
{
protected:
    unsigned long _eventStart = 0;     // Timestamp for when the current state was entered
    bool          _active     = false; // Should we be drawing?
    portMUX_TYPE  _mux = portMUX_INITIALIZER_UNLOCKED; // A mutex since we also touch vars under
                                                       // interrupt

    bool              _hasIRQHandlerFiredYet = false;
    int               _lastIRQButtonState    = HIGH;
    uint32_t          _lastIRQTimeMs         = 0;
    volatile uint32_t _irqCount              = 0; // total IRQs seen on this pin (diagnostic)

    uint8_t _buttonPin1 = PIN_NONE;
    uint8_t _buttonPin2 = PIN_NONE;

    static constexpr uint32_t DebounceMs = 30;

    LEDStripGFX* _pStrip = nullptr;

    bool SecondaryInputAllowsStart() const
    {
        return _buttonPin2 == PIN_NONE || IsInputPressed(_buttonPin2);
    }

    void AcknowledgePendingIRQ()
    {
        portENTER_CRITICAL(&_mux);
        _hasIRQHandlerFiredYet = false;
        portEXIT_CRITICAL(&_mux);
    }

public:
    LightingEvent(LEDStripGFX* pStrip, uint8_t buttonPin1, uint8_t buttonPin2 = PIN_NONE)
    {
        _pStrip     = pStrip;
        _eventStart = 0;
        _active     = false;
        _buttonPin1 = buttonPin1;
        _buttonPin2 = buttonPin2;
    }

    void IRQ()
    {
        portENTER_CRITICAL_ISR(&_mux);
        int newState           = digitalRead(_buttonPin1);
        _lastIRQButtonState    = newState; // Save last button state
        _lastIRQTimeMs         = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;
        _hasIRQHandlerFiredYet = true; // Increment count of IRQs in the current state
        _irqCount++;
        portEXIT_CRITICAL_ISR(&_mux);
    }

    uint8_t  GetPin() const { return _buttonPin1; }
    uint32_t GetIRQCount() const { return _irqCount; }

    void SyncToInput()
    {
        if (_buttonPin1 == PIN_NONE)
            return;

        if (IsInputPressed(_buttonPin1) && SecondaryInputAllowsStart())
            Begin();

        AcknowledgePendingIRQ();
    }

    // CheckForButtonPress
    //
    // Checks to see if the input is done bouncing. Active-low inputs begin the
    // effect when asserted and end it when released.

    void CheckForButtonPress()
    {
        if (_buttonPin1 == PIN_NONE)
            return;

        int currentState = digitalRead(_buttonPin1);

        portENTER_CRITICAL(&_mux);
        bool     save             = _hasIRQHandlerFiredYet;
        uint32_t saveLastIRQTime  = _lastIRQTimeMs;
        int      saveLastIRQState = _lastIRQButtonState;
        portEXIT_CRITICAL(&_mux);

        // Don't enable this except for temporary debugging, it'd be far too often
        // and verbose otherwise
        //
        // Serial.printf("Buttons: 36-%u 37-%u 38-%u 39-%u\n", digitalRead(36),
        // digitalRead(37), digitalRead(38), digitalRead(39));

        // If we have interrupts indicating state changes, wait until the input
        // has settled long enough to count as a real transition.

        if ((save != 0) && (currentState == saveLastIRQState) &&
            (millis() - saveLastIRQTime > DebounceMs))
        {
            // Active-LOW inputs: asserted == LOW, released == HIGH.
            if (currentState == LOW && SecondaryInputAllowsStart())
            {
                Begin();
            }
            else if (GetActive())
            {
                End();
            }

            AcknowledgePendingIRQ();
        }
    }

    // TimeElapsedTotal
    //
    // Total time event has been running in fractional seconds

    float TimeElapsedTotal() const { return (millis() - _eventStart) / 1000.0f; }

    bool GetActive() const { return _active; }

    void SetActive(bool bActive) { _active = bActive; }

    virtual void Begin()
    {
        if (!_active)
            _eventStart = millis();

        _active = true;
    };

    virtual void End()
    {
        _active     = false;
        _eventStart = millis();
    };

    virtual void Draw() = 0;
};

// BackupEvent
//
// Draws the strip as white

class BackupEvent : public LightingEvent
{
    static constexpr float BloomTime = 0.25f;

public:
    BackupEvent(LEDStripGFX* pStrip, uint8_t buttonPin1, uint8_t buttonPin2 = PIN_NONE)
        : LightingEvent(pStrip, buttonPin1, buttonPin2)
    {
    }

    void Draw() override
    {
        if (false == GetActive())
            return;

        // The backup light illuminates the whole strip in white.  It quickly
        // "blooms" out from the center to fill the strip.

        float fPercentComplete = min(TimeElapsedTotal() / BloomTime, 1.0f);
        int   cLEDs            = NUMBER_USED_PIXELS * fPercentComplete;
        int   iFirst           = (NUMBER_USED_PIXELS / 2) - (cLEDs / 2);
        int   iLast            = (NUMBER_USED_PIXELS / 2) + (cLEDs / 2);

        for (int i = 0; i < NUMBER_USED_PIXELS; i++)
        {
            if (i < iFirst || i > iLast)
                _pStrip->drawPixel(i, CRGB::Black);
            else
                _pStrip->drawPixel(i, CRGB::White);
        }
    }
};

// BrakingEvent - CHMSL (Center High Mount Stop Light) - or ThirdBrakeLight

class BrakingEvent : public LightingEvent
{
    static constexpr float BrakeStrobeDuration = 0.5f;
    static constexpr float BloomStartSize      = 0.10f;
    static constexpr float BloomTime           = 0.25f;

public:
    BrakingEvent(LEDStripGFX* pStrip, uint8_t buttonPin1, uint8_t buttonPin2 = PIN_NONE)
        : LightingEvent(pStrip, buttonPin1, buttonPin2)
    {
    }

    // BrakingEvent::Draw
    //
    // The strobe flash happens too fast to wait for the loop pump system, so we
    // do a full 50ms cycle of it here (30 on, 20 off).  That way it is crisp and
    // accurate but we still never block the system for more than 50ms, which is
    // sort of a limit I've set

    void Draw() override
    {
        if (false == GetActive())
            return;

        if (TimeElapsedTotal() < BrakeStrobeDuration)
        {
            float timeElapsed   = TimeElapsedTotal();
            float pctComplete   = min(1.0f, (timeElapsed / BloomTime) + BloomStartSize);
            float unusedEachEnd = (1.0f - pctComplete) * NUMBER_USED_PIXELS / 2;

            bool bLit = (millis() / 40) % 2 == 1;

            for (uint16_t i = unusedEachEnd; i < NUMBER_USED_PIXELS - unusedEachEnd; i++)
                _pStrip->drawPixel(i, bLit ? CRGB::Red : CRGB(16, 0, 0));
        }
        else
        {
            for (uint16_t i = 1; i < NUMBER_USED_PIXELS; i++)
                _pStrip->drawPixel(i, CRGB::Red);
        }
    }
};

// SignalEvent
//
// Handles left turns, right turns, and standard hazards (simply both signals at
// once)

class SignalEvent : public LightingEvent
{
public:
    enum class Style : uint8_t
    {
        Invalid = 0,
        LeftTurn,
        RightTurn,
        Hazard
    };

private:
    static constexpr uint32_t FlashDurationMs = 1000;

    // SetTurnLED
    //
    // Depending on which way the signal is turning, light up it's LED on the
    // correct end of the light strip

    void SetTurnLED(int i, CRGB color)
    {
        if (i < 0 || i >= NUMBER_TURN_PIXELS)
            return;

        if (_style == Style::RightTurn || _style == Style::Hazard)
            _pStrip->drawPixel(i, color);

        if (_style == Style::LeftTurn || _style == Style::Hazard)
            _pStrip->drawPixel(NUMBER_USED_PIXELS - 1 - i, color);
    }

    Style _style = Style::Invalid;

public:
    SignalEvent(LEDStripGFX* pStrip, uint8_t buttonPin1, Style style, uint8_t buttonPin2 = PIN_NONE)
        : LightingEvent(pStrip, buttonPin1, buttonPin2), _style(style)
    {
    }

    // Signals are different in that they don't end immediately but instead at the
    // end of their cycle.  So when and End() is called we just keep track of that
    // fact so that we know any subsequent Begins() that come in after an End()
    // mean restart

    bool     _exitAtEnd = false;
    uint32_t _stopAtMs  = 0;

    void End() override
    {
        if (!_active || _exitAtEnd)
            return;

        const uint32_t elapsedMs   = millis() - _eventStart;
        const uint32_t remainingMs = FlashDurationMs - (elapsedMs % FlashDurationMs);

        _exitAtEnd = true;
        _stopAtMs  = millis() + remainingMs;
    };

    void Begin() override
    {
        if (!_active || _exitAtEnd)
            _eventStart = millis();

        _active    = true;
        _exitAtEnd = false;
    };

    void Draw() override
    {
        if (false == GetActive())
            return;

        if (_exitAtEnd && static_cast<int32_t>(millis() - _stopAtMs) >= 0)
        {
            _active    = false;
            _exitAtEnd = false;
            return;
        }

        const uint32_t elapsedMs = millis() - _eventStart;
        const float    fCyclePosition =
            (elapsedMs % FlashDurationMs) / static_cast<float>(FlashDurationMs);

        for (int i = 0; i < NUMBER_TURN_PIXELS; i++)
            SetTurnLED(i, CRGB::Black);

        for (int i = 0; i < NUMBER_TURN_PIXELS; i++)
        {
            int iPaletteStart = 240 * fCyclePosition; // 240 so that it can "wrap" around inside the
                                                      // palette at the end seamlessly
            float iPaletteStep = (NUMBER_USED_PIXELS / NUMBER_TURN_PIXELS) / 3.75f;

            CRGB color = ColorFromPalette(SignalColors_pal, iPaletteStart + i * iPaletteStep);
            SetTurnLED(i, color);
        }
    }
};

// PoliceLightBarState
//
// The police bar breaks the light strip into 8 sections, and then alternates
// patterns based on a table.

struct PoliceLightBarState
{
    uint32_t sectionColor[8];
    float    duration;
};

class PoliceLightBar : public LightingEvent
{
    inline static constexpr float LongPulse  = 0.30f;
    inline static constexpr float ShortPulse = 0.04f;

    inline static const std::array<PoliceLightBarState, 25> PoliceBarStates = {{
        {{CRGB::Blue, CRGB::Blue, CRGB::Red, CRGB::Red, CRGB::Blue, CRGB::Blue, CRGB::Red,
          CRGB::Red},
         LongPulse},
        {{CRGB::Red, CRGB::Red, CRGB::Blue, CRGB::Blue, CRGB::Red, CRGB::Red, CRGB::Blue,
          CRGB::Blue},
         LongPulse},
        {{CRGB::Blue, CRGB::Blue, CRGB::Red, CRGB::Red, CRGB::Blue, CRGB::Blue, CRGB::Red,
          CRGB::Red},
         LongPulse},
        {{CRGB::Red, CRGB::Red, CRGB::Blue, CRGB::Blue, CRGB::Red, CRGB::Red, CRGB::Blue,
          CRGB::Blue},
         LongPulse},
        {{CRGB::Blue, CRGB::Blue, CRGB::Red, CRGB::Red, CRGB::Blue, CRGB::Blue, CRGB::Red,
          CRGB::Red},
         LongPulse},
        {{CRGB::Red, CRGB::Red, CRGB::Blue, CRGB::Blue, CRGB::Red, CRGB::Red, CRGB::Blue,
          CRGB::Blue},
         LongPulse},
        {{CRGB::Blue, CRGB::Blue, CRGB::Red, CRGB::Red, CRGB::Blue, CRGB::Blue, CRGB::Red,
          CRGB::Red},
         LongPulse},
        {{CRGB::Red, CRGB::Red, CRGB::Blue, CRGB::Blue, CRGB::Red, CRGB::Red, CRGB::Blue,
          CRGB::Blue},
         LongPulse},
        {{CRGB::Blue, CRGB::Blue, CRGB::Red, CRGB::Red, CRGB::Blue, CRGB::Blue, CRGB::Red,
          CRGB::Red},
         LongPulse},
        {{CRGB::Red, CRGB::Red, CRGB::Blue, CRGB::Blue, CRGB::Red, CRGB::Red, CRGB::Blue,
          CRGB::Blue},
         LongPulse},

        {{CRGB::White, CRGB::Blue, CRGB::Red, CRGB::Red, CRGB::Blue, CRGB::Blue, CRGB::Red,
          CRGB::Red},
         ShortPulse},
        {{CRGB::Blue, CRGB::Blue, CRGB::Red, CRGB::Red, CRGB::Blue, CRGB::Blue, CRGB::Red,
          CRGB::White},
         ShortPulse},
        {{CRGB::Blue, CRGB::White, CRGB::Red, CRGB::Red, CRGB::Blue, CRGB::Blue, CRGB::Red,
          CRGB::Red},
         ShortPulse},
        {{CRGB::Blue, CRGB::Blue, CRGB::Red, CRGB::Red, CRGB::Blue, CRGB::Blue, CRGB::White,
          CRGB::Red},
         ShortPulse},
        {{CRGB::Blue, CRGB::Blue, CRGB::White, CRGB::Red, CRGB::Blue, CRGB::Blue, CRGB::Red,
          CRGB::Red},
         ShortPulse},
        {{CRGB::Blue, CRGB::Blue, CRGB::Red, CRGB::Red, CRGB::Blue, CRGB::White, CRGB::Red,
          CRGB::Red},
         ShortPulse},
        {{CRGB::Blue, CRGB::Blue, CRGB::Red, CRGB::White, CRGB::Blue, CRGB::Blue, CRGB::Red,
          CRGB::Red},
         ShortPulse},
        {{CRGB::Blue, CRGB::Blue, CRGB::Red, CRGB::Red, CRGB::White, CRGB::Blue, CRGB::Red,
          CRGB::Red},
         ShortPulse},
        {{CRGB::Blue, CRGB::Blue, CRGB::Red, CRGB::White, CRGB::Blue, CRGB::Blue, CRGB::Red,
          CRGB::Red},
         ShortPulse},
        {{CRGB::Blue, CRGB::Blue, CRGB::Red, CRGB::Red, CRGB::Blue, CRGB::White, CRGB::Red,
          CRGB::Red},
         ShortPulse},
        {{CRGB::Blue, CRGB::Blue, CRGB::White, CRGB::Red, CRGB::Blue, CRGB::Blue, CRGB::Red,
          CRGB::Red},
         ShortPulse},
        {{CRGB::Blue, CRGB::Blue, CRGB::Red, CRGB::Red, CRGB::Blue, CRGB::Blue, CRGB::White,
          CRGB::Red},
         ShortPulse},
        {{CRGB::Blue, CRGB::White, CRGB::Red, CRGB::Red, CRGB::Blue, CRGB::Blue, CRGB::Red,
          CRGB::Red},
         ShortPulse},
        {{CRGB::Blue, CRGB::Blue, CRGB::Red, CRGB::Red, CRGB::Blue, CRGB::Blue, CRGB::Red,
          CRGB::White},
         ShortPulse},
        {{CRGB::White, CRGB::Blue, CRGB::Red, CRGB::Red, CRGB::Blue, CRGB::Blue, CRGB::Red,
          CRGB::Red},
         ShortPulse},
    }};

    float _totalCycleTime = 0.0f;

public:
    PoliceLightBar(LEDStripGFX* pStrip, uint8_t buttonPin1, uint8_t buttonPin2 = PIN_NONE)
        : LightingEvent(pStrip, buttonPin1, buttonPin2)
    {
        for (const auto& state : PoliceBarStates)
            _totalCycleTime += state.duration;
    }

    void Begin() override
    {
        if (_active)
            LightingEvent::End();
        else
            LightingEvent::Begin();
    }

    void End() override {}

    void Draw() override
    {
        if (false == GetActive())
            return;

        float        fCyclePosition = fmod(TimeElapsedTotal(), _totalCycleTime);
        const size_t sectionSize    = NUMBER_USED_PIXELS / 8;

        // Find out which row of the table we're in based on how far into the
        // animation we are

        float  sofar = 0.0f;
        size_t row   = 0;
        while ((row + 1) < PoliceBarStates.size() &&
               (sofar + PoliceBarStates[row].duration) <= fCyclePosition)
        {
            sofar += PoliceBarStates[row].duration;
            row++;
        }

        // Draw the current frame

        for (uint16_t i = 0; i < NUMBER_USED_PIXELS; i++)
        {
            int            iSection = i / sectionSize;
            const uint32_t color    = PoliceBarStates[row].sectionColor[iSection];
            _pStrip->drawPixel(i, color);
        }
    }
};
