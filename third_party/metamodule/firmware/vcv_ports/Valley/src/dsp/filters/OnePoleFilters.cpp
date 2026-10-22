#include "OnePoleFilters.hpp"
#include <cassert>

OnePoleLPFilter::OnePoleLPFilter(FLOAT cutoffFreq, FLOAT initSampleRate) {
    setSampleRate(initSampleRate);
    setCutoffFreq(cutoffFreq);
}

FLOAT OnePoleLPFilter::process() {
    _z =  _a * input + _z * _b;
    output = _z;
    return output;
}

void OnePoleLPFilter::clear() {
    input = 0.0;
    _z = 0.0;
    output = 0.0;
}

void OnePoleLPFilter::setSampleRate(FLOAT sampleRate) {
    assert(sampleRate > 0.0);

    _sampleRate = sampleRate;
    _1_sampleRate = 1.0 / sampleRate;
    _maxCutoffFreq = sampleRate / 2.0 - 1.0;
    setCutoffFreq(_cutoffFreq);
}

void OnePoleLPFilter::setCutoffFreq(FLOAT cutoffFreq) {
    if (cutoffFreq == _cutoffFreq) {
        return;
    }

    assert(cutoffFreq > 0.0);
    assert(cutoffFreq <= _maxCutoffFreq);

    _cutoffFreq = cutoffFreq;
    _b = expf(-_2M_PI * _cutoffFreq * _1_sampleRate);
    _a = 1.0 - _b;
}

FLOAT OnePoleLPFilter::getMaxCutoffFreq() const {
    return _maxCutoffFreq;
}

//////////////////////////////////////////////////////////////////////////////////////////////////

OnePoleHPFilter::OnePoleHPFilter(FLOAT initCutoffFreq, FLOAT initSampleRate) {
    setSampleRate(initSampleRate);
    setCutoffFreq(initCutoffFreq);
    clear();
}

FLOAT OnePoleHPFilter::process() {
    _x0 = input;
    _y0 = _a0 * _x0 + _a1 * _x1 + _b1 * _y1;
    _y1 = _y0;
    _x1 = _x0;
    output = _y0;
    return _y0;
}

void OnePoleHPFilter::clear() {
    input = 0.0;
    output = 0.0;
    _x0 = 0.0;
    _x1 = 0.0;
    _y0 = 0.0;
    _y1 = 0.0;
}

void OnePoleHPFilter::setCutoffFreq(FLOAT cutoffFreq) {
    if (cutoffFreq == _cutoffFreq) {
        return;
    }

    assert(cutoffFreq > 0.0);
    assert(cutoffFreq <= _maxCutoffFreq);

    _cutoffFreq = cutoffFreq;
    _b1 = expf(-_2M_PI * _cutoffFreq * _1_sampleRate);
    _a0 = (1.0 + _b1) / 2.0;
    _a1 = -_a0;
}

void OnePoleHPFilter::setSampleRate(FLOAT sampleRate) {
    _sampleRate = sampleRate;
    _1_sampleRate = 1.0 / _sampleRate;
    _maxCutoffFreq = sampleRate / 2.0 - 1.0;
    setCutoffFreq(_cutoffFreq);
    clear();
}

DCBlocker::DCBlocker() {
    setSampleRate(44100.0);
    setCutoffFreq(20.f);
    clear();
}

DCBlocker::DCBlocker(FLOAT cutoffFreq) {
    setSampleRate(44100.0);
    setCutoffFreq(cutoffFreq);
    clear();
}

FLOAT DCBlocker::process(FLOAT input) {
    output = input - _z + _b * output;
    _z = input;
    return output;
}

void DCBlocker::clear() {
    _z = 0.0;
    output = 0.0;
}

void DCBlocker::setSampleRate(FLOAT sampleRate) {
    _sampleRate = sampleRate;
    _maxCutoffFreq = sampleRate / 2.0;
    setCutoffFreq(_cutoffFreq);
}

void DCBlocker::setCutoffFreq(FLOAT cutoffFreq) {
    _cutoffFreq = cutoffFreq;
    _b = 0.999;
}

FLOAT DCBlocker::getMaxCutoffFreq() const {
    return _maxCutoffFreq;
}
