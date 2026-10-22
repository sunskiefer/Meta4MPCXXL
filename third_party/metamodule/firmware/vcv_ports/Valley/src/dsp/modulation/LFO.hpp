#ifndef DSJ_LFO_HPP
#define DSJ_LFO_HPP
#include <vector>
#include <cmath>
#include <cstdint>

template<typename F = float>
class LFO {
public:
    F output = 0.0;
    F phase = 0.0;

    LFO() {
        _frequency = 1.0;
        _sampleRate = 44100.0;
        _stepSize = _frequency * (F)kTableLength / _sampleRate;
        for(auto i = 0; i < kTableLength; ++i) {
            _sine.push_back(sin(2.0 * M_PI * (F)i / (F)kTableLength));
        }
        _phasor = 0.0;
    }

    F process() {
        _plusPhase = _phasor + phase * kTableLength;
        if(_plusPhase < 0.0) {
            _plusPhase += kTableLength;
        }
        else if(_plusPhase >= kTableLength) {
            _plusPhase -= kTableLength;
        }

        _a = (long)_plusPhase;
        _frac = _plusPhase - _a;
        _b = _a + 1.0;
        _b %= kTableLength;
        output = _sine[_a] * (1.0 - _frac) + _sine[_b] * _frac;

        _phasor += _stepSize;
        if(_phasor >= kTableLength) {
            _phasor -= kTableLength;
        }
        return output;
    }

    void setFrequency(F frequency) {
        _frequency = frequency;
        calcStepSize();
    }
    void setSamplerate(F sampleRate) {
        _sampleRate = sampleRate;
        calcStepSize();
    }
private:
    F _frequency;
    F _sampleRate;
    F _stepSize;
    F _phasor;
    F _plusPhase;
    long _a, _b;
    F _frac;
    const long kTableLength = 4096;
    std::vector<F> _sine;

    void calcStepSize() {
        _stepSize = _frequency * (F)kTableLength / _sampleRate;
    }
};

template<typename F = float>
class TriSawLFO {
public:
    TriSawLFO(F sampleRate = 44100.0, F frequency = 1.0) {
        phase = 0.0;
        _output = 0.0;
        _sampleRate = sampleRate;
        _step = 0.0;
        _rising = true;
        setFrequency(frequency);
        setRevPoint(0.5);
    }

    F process() {
        if(_step > 1.0) {
            _step -= 1.0;
            _rising = true;
        }

        if(_step >= _revPoint) {
            _rising = false;
        }

        if(_rising) {
            _output = _step * _riseRate;
        }
        else {
            _output = _step * _fallRate - _fallRate;
        }

        _step += _stepSize;
        _output *= 2.0;
        _output -= 1.0;
        return _output;
    }

    void setFrequency(F frequency) {
        if (frequency == _frequency) {
            return;
        }
        _frequency = frequency;
        calcStepSize();
    }

    void setRevPoint(F revPoint) {
        _revPoint = revPoint;
        if(_revPoint < 0.0001) {
            _revPoint = 0.0001;
        }
        if(_revPoint > 0.999) {
            _revPoint = 0.999;
        }

        _riseRate = 1.0 / _revPoint;
        _fallRate = -1.0 / (1.0 - _revPoint);
    }

    void setSamplerate(F sampleRate) {
        _sampleRate = sampleRate;
        calcStepSize();
    }

    F getOutput() const {
        return _output;
    }

    F phase;

private:
    F _output;
    F _sampleRate;
    F _frequency = 0.0;
    F _revPoint;
    F _riseRate;
    F _fallRate;
    F _step;
    F _stepSize;
    bool _rising;

    void calcStepSize() {
        _stepSize = _frequency / _sampleRate;
    }
};

#endif
