//
// This plate reverb is based upon Jon Dattorro's 1997 reverb algorithm.
//

#pragma once
#include "../dsp/delays/AllpassFilter.hpp"
#include "../dsp/filters/OnePoleFilters.hpp"
#include "../dsp/modulation/LFO.hpp"
// #include <iostream>
#include <array>

#ifdef USE_SINGLE_FLOAT
using FLOAT = float;
#else
using FLOAT = double;
#endif

class Dattorro1997Tank {

public:
    Dattorro1997Tank(const FLOAT initMaxSampleRate = 44100.0,
                     const FLOAT initMaxLfoDepth = 0.0,
                     const FLOAT initMaxTimeScale = 1.0);

    void process(const FLOAT leftInput, const FLOAT rightIn,
                 FLOAT* leftOut, FLOAT* rightOut);

    void freeze(const bool freezeFlag);

    void setSampleRate(const FLOAT newSampleRate);
    void setTimeScale(const FLOAT newTimeScale);

    void setDecay(const FLOAT newDecay);

    void setModSpeed(const FLOAT newModSpeed);
    void setModDepth(const FLOAT newModDepth);
    void setModShape(const FLOAT shape);

    void setHighCutFrequency(const FLOAT frequency);
    void setLowCutFrequency(const FLOAT frequency);

    void setDiffusion(const FLOAT diffusion);

    void clear();
    bool clear_step(unsigned clear_block);

private:
    static constexpr FLOAT dattorroSampleRate = 29761.0;
    static constexpr FLOAT dattorroSampleTime = 1.0 / dattorroSampleRate;

    static constexpr FLOAT leftApf1Time = 672.0;
    static constexpr FLOAT leftDelay1Time = 4453.0;
    static constexpr FLOAT leftApf2Time = 1800.0;
    static constexpr FLOAT leftDelay2Time = 3720.0;

    static constexpr FLOAT rightApf1Time = 908.0;
    static constexpr FLOAT rightDelay1Time = 4217.0;
    static constexpr FLOAT rightApf2Time = 2656.0;
    static constexpr FLOAT rightDelay2Time = 3163.0;

    static constexpr size_t leftDelay1RightTap1 = 266;
    static constexpr size_t leftDelay1RightTap2 = 2974;
    static constexpr size_t leftApf2RightTap1 = 1913;
    static constexpr size_t leftDelay2RightTap = 1996;
    static constexpr size_t rightDelay1RightTap = 1990;
    static constexpr size_t rightApf2RightTap = 187;
    static constexpr size_t rightDelay2RightTap = 1066;

    enum LeftOutTaps {
        L_DELAY_1_L_TAP_1,
        L_DELAY_1_L_TAP_2,
        L_APF_2_L_TAP,
        L_DELAY_2_L_TAP,
        R_DELAY_1_L_TAP,
        R_APF_2_L_TAP,
        R_DELAY_2_L_TAP
    };

    enum RightOutTaps {
        R_DELAY_1_R_TAP_1,
        R_DELAY_1_R_TAP_2,
        R_APF_2_R_TAP,
        R_DELAY_2_R_TAP,
        L_DELAY_1_R_TAP,
        L_APF_2_R_TAP,
        L_DELAY_2_R_TAP
    };

    const long kOutputTaps[7] = {266, 2974, 1913, 1996, 1990, 187, 1066};

    static constexpr FLOAT maxDiffusion1 = 0.7;
    static constexpr FLOAT maxDiffusion2 = 0.7;

    static constexpr FLOAT lfoMaxExcursion = 16.0;
    static constexpr FLOAT lfo1Freq = 0.10;
    static constexpr FLOAT lfo2Freq = 0.150;
    static constexpr FLOAT lfo3Freq = 0.120;
    static constexpr FLOAT lfo4Freq = 0.180;

    static constexpr FLOAT minTimeScale = 0.0001;

    FLOAT timePadding = 0.0;

    FLOAT scaledLeftApf1Time = leftApf1Time;
    FLOAT scaledLeftDelay1Time = leftDelay1Time;
    FLOAT scaledLeftApf2Time = leftApf2Time;
    FLOAT scaledLeftDelay2Time = leftDelay2Time;

    FLOAT scaledRightApf1Time = rightApf1Time;
    FLOAT scaledRightDelay1Time = rightDelay1Time;
    FLOAT scaledRightApf2Time = rightApf2Time;
    FLOAT scaledRightDelay2Time = rightDelay2Time;

    std::array<long, 7> scaledOutputTaps;

    FLOAT maxSampleRate = 44100.;
    FLOAT sampleRate = maxSampleRate;
    FLOAT sampleRateScale = sampleRate / dattorroSampleRate;

    FLOAT maxTimeScale = 1.0;
    FLOAT timeScale = 1.0;

    FLOAT modDepth = 0.0;
    FLOAT decayParam = 0.0;
    FLOAT decay = 0.0;

    FLOAT lfoExcursion = 0.0;

    // Freeze Cross fade
    bool frozen = false;
    FLOAT fade = 1.0;
    FLOAT fadeTime = 0.002;
    FLOAT fadeStep = 1.0 / (fadeTime * sampleRate);
    FLOAT fadeDir = 1.0;

    TriSawLFO<FLOAT> lfo1;
    TriSawLFO<FLOAT> lfo2;
    TriSawLFO<FLOAT> lfo3;
    TriSawLFO<FLOAT> lfo4;

    FLOAT leftSum = 0.0;
    FLOAT rightSum = 0.0;

    AllpassFilter<FLOAT> leftApf1;
    InterpDelay<FLOAT> leftDelay1;
    OnePoleLPFilter leftHighCutFilter;
    OnePoleHPFilter leftLowCutFilter;
    AllpassFilter<FLOAT> leftApf2;
    InterpDelay<FLOAT> leftDelay2;

    AllpassFilter<FLOAT> rightApf1;
    InterpDelay<FLOAT> rightDelay1;
    OnePoleLPFilter rightHighCutFilter;
    OnePoleHPFilter rightLowCutFilter;
    AllpassFilter<FLOAT> rightApf2;
    InterpDelay<FLOAT> rightDelay2;

    OnePoleHPFilter leftOutDCBlock;
    OnePoleHPFilter rightOutDCBlock;

    void initialiseDelaysAndApfs();

    void tickApfModulation();

    void rescaleApfAndDelayTimes();
    void rescaleTapTimes();
};

class Dattorro {
public:
    Dattorro(const FLOAT initMaxSampleRate = 44100.0,
             const FLOAT initMaxLfoDepth = 16.0,
             const FLOAT initMaxTimeScale = 1.0);
    void process(FLOAT leftInput, FLOAT rightInput);
    void clear();
    void clear_start();
    bool clear_step();

    void setTimeScale(FLOAT timeScale);
    void setPreDelay(FLOAT time);
    void setSampleRate(FLOAT sampleRate);

    void freeze(const bool freezeFlag);

    void setInputFilterLowCutoffPitch(FLOAT pitch);
    void setInputFilterHighCutoffPitch(FLOAT pitch);
    void enableInputDiffusion(bool enable);

    void setDecay(FLOAT newDecay);
    void setTankDiffusion(const FLOAT diffusion);
    void setTankFilterHighCutFrequency(const FLOAT frequency);
    void setTankFilterLowCutFrequency(const FLOAT frequency);

    void setTankModSpeed(const FLOAT modSpeed);
    void setTankModDepth(const FLOAT modDepth);
    void setTankModShape(const FLOAT modShape);

    FLOAT getLeftOutput() const;
    FLOAT getRightOutput() const;

private:
    FLOAT preDelayTime = 0.0;
    static constexpr long kInApf1Time = 141;
    static constexpr long kInApf2Time = 107;
    static constexpr long kInApf3Time = 379;
    static constexpr long kInApf4Time = 277;

    static constexpr FLOAT dattorroSampleRate = 29761.0;
    FLOAT sampleRate = 44100.0;
    FLOAT dattorroScaleFactor = sampleRate / dattorroSampleRate;

    FLOAT rightOut = 0.0;
    FLOAT leftOut = 0.0;
    FLOAT inputLowCut = 0.0;
    FLOAT inputHighCut = 10000.0;
    FLOAT inputDiffusion1 = 0.75;
    FLOAT inputDiffusion2 = 0.625;
    FLOAT decay = 0.9999;
    FLOAT diffuseInput = 0.0;

    OnePoleHPFilter leftInputDCBlock;
    OnePoleHPFilter rightInputDCBlock;
    OnePoleLPFilter inputLpf;
    OnePoleHPFilter inputHpf;

    InterpDelay<FLOAT> preDelay;

    AllpassFilter<FLOAT> inApf1;
    AllpassFilter<FLOAT> inApf2;
    AllpassFilter<FLOAT> inApf3;
    AllpassFilter<FLOAT> inApf4;

    Dattorro1997Tank tank;

    FLOAT tankFeed = 0.0;

    unsigned clear_block_ctr = 0;

    FLOAT dattorroScale(FLOAT delayTime);
};

