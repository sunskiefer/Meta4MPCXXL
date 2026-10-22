#pragma once
#include <cmath>
#include <stdio.h>
#include <cstdint>

#define _1_FACT_2 0.5
#define _1_FACT_3 0.1666666667
#define _1_FACT_4 0.04166666667
#define _1_FACT_5 0.008333333333
#define _2M_PI 2.0 * M_PI

template<typename T>
T fastexp(T x) {
    T xx = x * x;
    T x3 = x * xx;
    T x4 = xx * xx;
    T x5 = x4 * x;
    x = 1 + x + (xx * _1_FACT_2) + (x3 * _1_FACT_3) + (x4 * _1_FACT_4);
    return x + (x5 * _1_FACT_5);
}

#ifdef USE_SINGLE_FLOAT
using FLOAT = float;
#else
using FLOAT = double;
#endif

class OnePoleLPFilter {
public:
    OnePoleLPFilter(FLOAT cutoffFreq = 22049.0, FLOAT initSampleRate = 44100.0);
    FLOAT process();
    void clear();
    void setCutoffFreq(FLOAT cutoffFreq);
    void setSampleRate(FLOAT sampleRate);
    FLOAT getMaxCutoffFreq() const;
    FLOAT input = 0.0;
    FLOAT output = 0.0;
private:
    FLOAT _sampleRate = 44100.0;
    FLOAT _1_sampleRate = 1.0 / _sampleRate;
    FLOAT _cutoffFreq = 0.0;
    FLOAT _maxCutoffFreq = _sampleRate / 2.0;
    FLOAT _a = 0.0;
    FLOAT _b = 0.0;
    FLOAT _z = 0.0;
};

//////////////////////////////////////////////////////////////////////////////////////////////////

class OnePoleHPFilter {
public:
    OnePoleHPFilter(FLOAT initCutoffFreq = 10.0, FLOAT initSampleRate = 44100.0);
    FLOAT process();
    void clear();
    void setCutoffFreq(FLOAT cutoffFreq);
    void setSampleRate(FLOAT sampleRate);
    FLOAT input = 0.0;
    FLOAT output = 0.0;
private:
    FLOAT _sampleRate = 0.0;
    FLOAT _1_sampleRate = 0.0;
    FLOAT _cutoffFreq = 0.0;
    FLOAT _maxCutoffFreq = _sampleRate / 2.0 - 1.0;
    FLOAT _y0 = 0.0;
    FLOAT _y1 = 0.0;
    FLOAT _x0 = 0.0;
    FLOAT _x1 = 0.0;
    FLOAT _a0 = 0.0;
    FLOAT _a1 = 0.0;
    FLOAT _b1 = 0.0;
};

class DCBlocker {
public:
    DCBlocker();
    DCBlocker(FLOAT cutoffFreq);
    FLOAT process(FLOAT input);
    void clear();
    void setCutoffFreq(FLOAT cutoffFreq);
    void setSampleRate(FLOAT sampleRate);
    FLOAT getMaxCutoffFreq() const;
    FLOAT output;
private:
    FLOAT _sampleRate;
    FLOAT _cutoffFreq;
    FLOAT _maxCutoffFreq;
    FLOAT _b;
    FLOAT _z;
};
