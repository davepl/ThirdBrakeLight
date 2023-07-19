
#pragma once
#include <Arduino.h>
#include <sys/time.h>

#define USE_TFT          1
#define SMALL_TFT        1

#define MATRIX_WIDTH     70    
#define MATRIX_HEIGHT    1

#define LED_PIN          2

#define LEFT_TURN_PIN    36
#define RIGHT_TURN_PIN   37
#define BACKUP_PIN       38
#define EMERGENCY_PIN    39

#define NUMBER_USED_PIXELS  (MATRIX_WIDTH*MATRIX_HEIGHT) 
#define NUMBER_TURN_PIXELS  (NUMBER_USED_PIXELS / 3.333)

#define MICROS_PER_SECOND   1000000
#define MILLIS_PER_SECOND   1000

#define ARRAYSIZE(a)		(sizeof(a)/sizeof(a[0]))		// Returns the number of elements in an array
#define PERIOD_FROM_FREQ(f) (round(1000000 * (1.0 / f)))	// Calculate period in microseconds (us) from frequency in Hz
#define FREQ_FROM_PERIOD(p) (1.0 / p * 1000000)				// Calculate frequency in Hz given the priod in microseconds (us)

#define MIN(x, y)    ((x < y) ? x : y)
#define MAX(x, y)    ((x > y) ? x : y)

inline static double randomDouble(double lower, double upper)
{
    double result = (lower + ((upper - lower) * rand()) / RAND_MAX);
    return result;
}

inline double mapDouble(double x, double in_min, double in_max, double out_min, double out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

inline void vDelay(size_t millis)
{
    vTaskDelay(millis/portTICK_PERIOD_MS);
}

// FPS
// 
// Given a time value for when the last frame took place and the current timestamp returns the number of
// frames per second, as low as 0.  Never exceeds 999 so you can make some width assumptions.

inline int FPS(uint32_t start, uint32_t end, uint32_t perSecond = MILLIS_PER_SECOND)
{
	uint32_t duration = end - start;
	double fpsf = 1.0f / (duration / (double) perSecond);
	int FPS = (int)fpsf;
	if (FPS > 999)
		FPS = 999;
	return FPS;
}

inline void DelayedReboot() { Serial.flush(); delay(10000); exit(0); }  // For catastrophic failure, wait 10 seconds then reboot

// AppTime
//
// A class that keeps track of the clock, how long the last frame took, calculating FPS, etc.

class AppTime
{
  private:
    static double _lastFrame;
    static double _deltaTime;

  public:

    AppTime()
    {
        NewFrame();
    }

    double FrameStartTime() const
    {
        return _lastFrame;
    }

    static double CurrentTime()
    {
        timeval tv;
        gettimeofday(&tv, nullptr);
        return tv.tv_sec + (tv.tv_usec/1000000.0);
    }

    double DeltaTime() const
    {
        return _deltaTime;
    }

    // NewFrame
    //
    // Call this at the start of every frame or udpate, and it'll figure out and keep track of how
    // long between frames 

    void NewFrame()
    {
        timeval tv;
        gettimeofday(&tv, nullptr);
        double current = CurrentTime();
        _deltaTime = current - _lastFrame;

        // Cap the delta time at one full second

        if (_deltaTime > 1.0f)
            _deltaTime = 1.0f;

        _lastFrame = current;
    }
};
       
