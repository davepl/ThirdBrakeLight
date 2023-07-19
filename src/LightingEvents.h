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

#ifndef ARRAYSIZE
#define ARRAYSIZE(x) (sizeof(x)/sizeof(*x))
#endif

#define AMBERHI 0xFF5000	// Signal light base colors
#define AMBER1  0xFF3000				
#define AMBER2  0x801800
#define AMBER3  0x400C00
#define AMBER4  0x200600
#define AMBER5  0x100300

// We define a palete with a black background and he amber sweep in the middle.
// which will give us the effect of the signal "blooming" off and on.  The higher in the
// table that you define your colors, the earlier they will appear, which is safer.  This
// table is actually tuned to about the bloom rate of my 1157 signals, which wouldn't be
// legal for LEDs.  Pretty sure they need to come on "all in" and then animate from there, 
// based on what I see Audi and others doing, anyway.

const TProgmemRGBPalette16 SignalColors_p FL_PROGMEM =
{
    CRGB::Black,
    CRGB::Black,
    CRGB::Black,
    CRGB::Black,
    AMBERHI,
    AMBER1,
    AMBER1,
    AMBER2,
    AMBER3,
    AMBER4,
    AMBER5,
    CRGB::Black,
    CRGB::Black,
    CRGB::Black,
    CRGB::Black,

};

CRGBPalette256 SignalColors_pal = SignalColors_p;

// LightingEvent (eg: BrakingEvent, SignalEvent, etc)
//
// Base class for things like turn signals, braking, backing up, etc.
// All derived classes must implement the pure virtual Draw() function.
// All derived methods MUST call their base class implementations properly
// in order for the timers to work, etc.
//
// In short, you create a derived class and then call Begin() when the
// event starts (like braking), and Update keeps track of the currentr
// state.  Draw() actually renders the current state of the effect to
// the light strip.

class LightingEvent
{
protected:

    unsigned long       _eventStart = 0;    						// Timestamp for when the current state was entered
    bool                _active = false;    						// Should we be drawing?
    portMUX_TYPE 		_mux = portMUX_INITIALIZER_UNLOCKED;		// A mutex since we also touch vars under interrupt

    bool    			_hasIRQHandlerFiredYet = false;					
    int					_lastIRQButtonState = 0;
    int 				_debounceTimeout = 0;

    uint8_t				_buttonPin1 = -1;
    uint8_t             _buttonPin2 = 0;

    const int			DEBOUNCETIME = 30;

    LEDStripGFX       * _pStrip = nullptr;

  public:

    LightingEvent(LEDStripGFX * pStrip, uint8_t buttonPin1, uint8_t buttonPin2 = 0)  
    {
        _pStrip = pStrip;
        _eventStart = 0;
        _active = false;
        _buttonPin1 = buttonPin1;
        _buttonPin2 = buttonPin2;
    }

    void IRQ()
    {
          portENTER_CRITICAL_ISR(&_mux);
          int newState     = digitalRead(_buttonPin1);
          _lastIRQButtonState = newState;						// Save last button state
          _debounceTimeout = xTaskGetTickCount(); 			    // This version of millis() that works from interrupt
          _hasIRQHandlerFiredYet=true;							// Increment count of IRQs in the current state
          portEXIT_CRITICAL_ISR(&_mux);
    }
    
    // CheckForButtonPress
    //
    // Checks to see if button is done bouncing, in that it has remained steady state for some number of milliseconds,
    // like 30.  Then, if it has come to rest in a HIGH state, we begin the effect.  In the LOW state we end the effect.
    // Allows for an optional second button that is not debounced but rather only checked to see if it is ALSO high.

    void CheckForButtonPress()
    {
        if (_buttonPin1 == 0)
            return;

        bool bCurrentState = digitalRead(_buttonPin1);

        portENTER_CRITICAL_ISR(&_mux); 
        bool save  				 = _hasIRQHandlerFiredYet;
        int  saveDebounceTimeout = _debounceTimeout;
        bool saveLastIRQState  	 = _lastIRQButtonState;
        portEXIT_CRITICAL_ISR(&_mux); 

        // Don't enable this except for temporary debugging, it'd be far too often and verbose otherwise
        //
        // Serial.printf("Buttons: 36-%u 37-%u 38-%u 39-%u\n", digitalRead(36), digitalRead(37), digitalRead(38), digitalRead(39));

        // If we have some positive number of interrupts (which indicate any state changes) we wait until the button
        // has "settled" down enough that DEBOUNCETIME has passed without another state change.  If it's been in the
        // current state for that long we call it a press.

        if ((save != 0)
            && (bCurrentState == saveLastIRQState) 			// pin is still in the same state as when intr triggered
            && (millis() - saveDebounceTimeout > DEBOUNCETIME ))
        { 	
              if (bCurrentState == HIGH)
              {
                // If a second button is specified, we check to see that is is also down.  If no other button is
                // specified, or if both are indeed down, we raise the Begin() method.  Not this precludes the use
                // of Pin0 as an input, but that's not a good idea for other pin-strapping reasons anyway...	

                if (_buttonPin2 == 0 || (HIGH == digitalRead(_buttonPin2)))
                {
                    Begin();
                }
                else if (GetActive() == true)
                {
                    End();
                }
              }
              else
              {
                Serial.printf("Ending effect if active for %d\n", _buttonPin1);
                if (GetActive() == true)
                {
                    Serial.printf("Ending effect for %d\n", _buttonPin1);
                    End();
                }
              }

            portENTER_CRITICAL_ISR(&_mux); 	// can't change it unless, atomic - Critical section
            _hasIRQHandlerFiredYet = false; // acknowledge keypress and reset interrupt counter
            portEXIT_CRITICAL_ISR(&_mux);
        }
    }

    // TimeElapsedTotal
    //
    // Total time event has been running in fractional seconds

    float TimeElapsedTotal()						
    {
        return (millis() - _eventStart) / 1000.0f;
    }

    bool GetActive()
    {
        return _active;
    }

    void SetActive(bool bActive)
    {
        Serial.printf("SetActive for pin to state %d %d\n", _buttonPin1, bActive);
        _active = bActive;
    }

    virtual void Begin()    
    {
        Serial.printf("Got input from pin %d\n", _buttonPin1);

        if (!_active)
            _eventStart = millis();

        _active = true;
    };

    virtual void End()      
    {
        Serial.printf("   Ending input from pin %d\n", _buttonPin1);

        _active = false;
        _eventStart = millis();
    };

    virtual void Draw()    = 0;
};

// BackupEvent
//
// Draws the strip as white

class BackupEvent : public LightingEvent
{
    const float BLOOM_TIME = 0.25f;

  public:

    BackupEvent(LEDStripGFX * pStrip, uint8_t buttonPin1, uint8_t buttonPin2 = 0) : LightingEvent(pStrip, buttonPin1, buttonPin2)
    {
    }

    virtual void Draw() override
    {
        if (false == GetActive())
            return;

        // The backup light illuminates the whole strip in white.  It quickly "blooms"
        // out from the center to fill the strip.

        float fPercentComplete = min(TimeElapsedTotal() / BLOOM_TIME, 1.0f);
        int cLEDs  = NUMBER_USED_PIXELS * fPercentComplete;
        int iFirst = (NUMBER_USED_PIXELS / 2) - (cLEDs / 2);
        int iLast  = (NUMBER_USED_PIXELS / 2) + (cLEDs / 2);
        
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
    const float BRAKE_STROBE_DURATION = 0.5f;
    const float BLOOM_START_SIZE      = 0.10;
    const float BLOOM_TIME            = 0.25;

  public:

    BrakingEvent(LEDStripGFX * pStrip, uint8_t buttonPin1, uint8_t buttonPin2 = 0) : LightingEvent(pStrip, buttonPin1, buttonPin2)
    {
    }
    
    // BrakingEvent::Draw
    //
    // The strobe flash happens too fast to wait for the loop pump system, so we do a full 50ms
    // cycle of it here (30 on, 20 off).  That way it is crisp and accurate but we still never
    // block the system for more than 50ms, which is sort of a limit I've set

    virtual void Draw() override
    {
        if (false == GetActive())
            return;

        if (TimeElapsedTotal() < BRAKE_STROBE_DURATION)
        {
            float timeElapsed = TimeElapsedTotal();
            float pctComplete = min(1.0f, (timeElapsed / BLOOM_TIME) + BLOOM_START_SIZE);
            float unusedEachEnd = (1.0f - pctComplete) * NUMBER_USED_PIXELS / 2;

            bool bLit = (millis()/40) % 2 == 1;

            for (uint16_t i = unusedEachEnd; i < NUMBER_USED_PIXELS - unusedEachEnd; i++)
                _pStrip->drawPixel(i, bLit ? CRGB::Red : CRGB(16,0,0));
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
// Handles let turns, right turtns, and standard hazards (simply both signals at once)

class SignalEvent : public LightingEvent
{
  public:

    enum SIGNAL_STYLE
    {
        INVALID = 0,
        LEFT_TURN,
        RIGHT_TURN,
        HAZARD
    };

  private:

    const float SequentialBloomStart = 0.00f;
    const float SequentialBloomTime  = 0.350f;

    const float SequentialHoldStart  = SequentialBloomStart + SequentialBloomTime;
    const float SequentialHoldTime   = 0.35f;

    const float SequentialFadeStart  = SequentialHoldStart + SequentialHoldTime;
    const float SequentialFadeTime   = 0.150f;

    const float SequentialOffStart   = SequentialFadeStart + SequentialFadeTime;
    const float SequentialOffTime    = 0.25f;

    const float SequentialCycleTime  = SequentialOffStart + SequentialOffTime;

    // SetTurnLED
    //
    // Depending on which way the signal is turning, light up it's LED on the correct
    // end of the light strip

    void SetTurnLED(int i, CRGB color)
    {
        if (i < 0 || i >= NUMBER_TURN_PIXELS)
            return;

        if (_style == RIGHT_TURN || _style == HAZARD)
            _pStrip->drawPixel(i, color);

        if (_style == LEFT_TURN || _style == HAZARD)
            _pStrip->drawPixel(NUMBER_USED_PIXELS - 1 - i, color);
    }

    SIGNAL_STYLE _style;

  private:

    SignalEvent(LEDStripGFX * pStrip, uint8_t buttonPin1, uint8_t buttonPin2 = 0) : LightingEvent(pStrip, buttonPin1, buttonPin2)
    {
    }

  public:

    SignalEvent(LEDStripGFX * pStrip, uint8_t buttonPin1, SIGNAL_STYLE style, uint8_t buttonPin2 = 0) 
        : LightingEvent(pStrip, buttonPin1, buttonPin2),
          _style(style)
    {
    }

    const float FlashDurationSeconds = 1.0f;

    // Signals are different in that they don't end immediately but instead at the end of their cycle.  So when and End() is
    // called we just keep track of that fact so that we know any subsequent Begins() that come in after an End() mean restart

    bool _bExitatEnd = false;

    virtual void End() override
    {
        _bExitatEnd = true;
    };

    virtual void Begin() override
    {
        if (!_active || _bExitatEnd)
            _eventStart = millis();
        _active = true;
        _bExitatEnd = false;
    };

    virtual void Draw() override
    {
        if (false == GetActive())
            return;

        float fCyclePosition = TimeElapsedTotal() / FlashDurationSeconds;
        if (fCyclePosition > 1.0f)
        {
            if (_bExitatEnd)
            {
                _active = false;
                return;
            }
            else
            {
                fCyclePosition -= 1.0f;
            }
        }
        for (int i = 0; i < NUMBER_TURN_PIXELS; i++)
            SetTurnLED(i, CRGB::Black);

        for (int i = 0; i < NUMBER_TURN_PIXELS; i++)
        {
            int iPaletteStart = 240 * fCyclePosition;	// 240 so that it can "wrap" around inside the palette at the end seamlessly
            float iPaletteStep   = (NUMBER_USED_PIXELS/NUMBER_TURN_PIXELS) / 3.75f;

            CRGB color = ColorFromPalette(SignalColors_pal, iPaletteStart + i * iPaletteStep);
            SetTurnLED(i, color);
        }
    }
};

// PoliceLightBarState
//
// The police bar breaks the light strip into 8 sections, and then alternates patterns based on a table.  

struct PoliceLightBarState
{
    uint32_t sectionColor[8];
    float    duration;
};

class PoliceLightBar : public LightingEvent
{
    static const PoliceLightBarState _PoliceBarStates1[32];		// Sadly we must spec arraysize because its a forward declaration
    float _TotalCycleTime;

  public:  

    PoliceLightBar(LEDStripGFX * pStrip, uint8_t buttonPin1, uint8_t buttonPin2 = 0)
        : LightingEvent(pStrip, buttonPin1, buttonPin2)
    {
        _TotalCycleTime = 0.0f;
        for (int i = 0; i < ARRAYSIZE(_PoliceBarStates1); i++)
            _TotalCycleTime += _PoliceBarStates1[i].duration;
    }

    virtual void Begin() override
    {
        if (_active)
            LightingEvent::End();
        else
            LightingEvent::Begin();
    }

    virtual void End() override
    {
        ;
    }

    virtual void Draw() override
    {
        // Paranoid and unneeded, but that's how I feel about this emergency effect in general :-)

        if (false == GetActive())
            return;

        float fCyclePosition = fmod(TimeElapsedTotal(), _TotalCycleTime);
        const size_t sectionSize  = NUMBER_USED_PIXELS / 8;

        // Find out which row of the table we're in based on how far into the animation we are

        float sofar = 0.0f;
        int   row = 0;
        while (sofar < fCyclePosition && row < ARRAYSIZE(_PoliceBarStates1) - 1)
        {
            sofar += _PoliceBarStates1[row].duration;
            row++;
        }
        row--;

        // Draw the current frame

        for (uint16_t i = 0; i < NUMBER_USED_PIXELS; i++)
        {
            int iSection = i / sectionSize;
            const uint32_t color = (_PoliceBarStates1[row].sectionColor[iSection]);
            _pStrip->drawPixel(i, color);
        }
    }
};

const float longPulse = 0.30;
const float shortPulse = 0.04;

const PoliceLightBarState PoliceLightBar::_PoliceBarStates1[] =
{
    {  { CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red,   CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red   }, longPulse },
    {  { CRGB::Red,   CRGB::Red,   CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red,   CRGB::Blue,  CRGB::Blue  }, longPulse },
    {  { CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red,   CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red   }, longPulse },
    {  { CRGB::Red,   CRGB::Red,   CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red,   CRGB::Blue,  CRGB::Blue  }, longPulse },
    {  { CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red,   CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red   }, longPulse },
    {  { CRGB::Red,   CRGB::Red,   CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red,   CRGB::Blue,  CRGB::Blue  }, longPulse },
    {  { CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red,   CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red   }, longPulse },
    {  { CRGB::Red,   CRGB::Red,   CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red,   CRGB::Blue,  CRGB::Blue  }, longPulse },
    {  { CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red,   CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red   }, longPulse },
    {  { CRGB::Red,   CRGB::Red,   CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red,   CRGB::Blue,  CRGB::Blue  }, longPulse },

    {  { CRGB::White, CRGB::Blue,  CRGB::Red,   CRGB::Red,   CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red   }, shortPulse },
    {  { CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red,   CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::White }, shortPulse },
    {  { CRGB::Blue,  CRGB::White, CRGB::Red,   CRGB::Red,   CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red   }, shortPulse },
    {  { CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red,   CRGB::Blue,  CRGB::Blue,  CRGB::White, CRGB::Red   }, shortPulse },
    {  { CRGB::Blue,  CRGB::Blue,  CRGB::White, CRGB::Red,   CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red   }, shortPulse },
    {  { CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red,   CRGB::Blue,  CRGB::White, CRGB::Red,   CRGB::Red   }, shortPulse },
    {  { CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::White, CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red   }, shortPulse },
    {  { CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red,   CRGB::White, CRGB::Blue,  CRGB::Red,   CRGB::Red   }, shortPulse },
    {  { CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::White, CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red   }, shortPulse },
    {  { CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red,   CRGB::Blue,  CRGB::White, CRGB::Red,   CRGB::Red   }, shortPulse },
    {  { CRGB::Blue,  CRGB::Blue,  CRGB::White, CRGB::Red,   CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red   }, shortPulse },
    {  { CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red,   CRGB::Blue,  CRGB::Blue,  CRGB::White, CRGB::Red   }, shortPulse },
    {  { CRGB::Blue,  CRGB::White, CRGB::Red,   CRGB::Red,   CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red   }, shortPulse },
    {  { CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red,   CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::White }, shortPulse },
    {  { CRGB::White, CRGB::Blue,  CRGB::Red,   CRGB::Red,   CRGB::Blue,  CRGB::Blue,  CRGB::Red,   CRGB::Red   }, shortPulse },
};

