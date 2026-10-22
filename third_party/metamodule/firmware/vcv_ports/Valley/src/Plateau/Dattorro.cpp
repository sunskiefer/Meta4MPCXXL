#include "Dattorro.hpp"
#include "utilities/math_lut.hh"
#include <cassert>
#include <algorithm>

Dattorro1997Tank::Dattorro1997Tank(const FLOAT initSampleRate,
                                   const FLOAT initMaxLfoDepth,
                                   const FLOAT initMaxTimeScale) :
    maxTimeScale(initMaxTimeScale) 
{
    timePadding = initMaxLfoDepth;
    setSampleRate(initSampleRate);

    leftOutDCBlock.setCutoffFreq(20.0);
    rightOutDCBlock.setCutoffFreq(20.0);

    lfo1.setFrequency(lfo1Freq);
    lfo2.setFrequency(lfo2Freq);
    lfo3.setFrequency(lfo3Freq);
    lfo4.setFrequency(lfo4Freq);

    lfo2.phase = 0.25;
    lfo3.phase = 0.5;
    lfo4.phase = 0.75;

    lfo1.setRevPoint(0.5);
    lfo2.setRevPoint(0.5);
    lfo3.setRevPoint(0.5);
    lfo4.setRevPoint(0.5);
}

void Dattorro1997Tank::process(const FLOAT leftIn, const FLOAT rightIn,
                               FLOAT* leftOut, FLOAT* rightOut) {
    tickApfModulation();

    decay = frozen ? 1.0 : decayParam;

    leftSum += leftIn;
    rightSum += rightIn;

    leftApf1.input = leftSum;
    leftDelay1.input = leftApf1.process();
    leftDelay1.process();
    leftHighCutFilter.input = leftDelay1.output;
    leftLowCutFilter.input = leftHighCutFilter.process();
    leftApf2.input = (leftDelay1.output * (1.0 - fade) + leftLowCutFilter.process() * fade) * decay;
    leftDelay2.input = leftApf2.process();
    leftDelay2.process();

    rightApf1.input = rightSum;
    rightDelay1.input = rightApf1.process();
    rightDelay1.process();
    rightHighCutFilter.input = rightDelay1.output;
    rightLowCutFilter.input =  rightHighCutFilter.process();
    rightApf2.input = (rightDelay1.output * (1.0 - fade) + rightLowCutFilter.process() * fade) * decay;
    rightDelay2.input = rightApf2.process();
    rightDelay2.process();

    rightSum = leftDelay2.output * decay;
    leftSum = rightDelay2.output * decay;

    leftOutDCBlock.input = leftApf1.output;
    leftOutDCBlock.input += leftDelay1.tap(scaledOutputTaps[L_DELAY_1_L_TAP_1]);
    leftOutDCBlock.input += leftDelay1.tap(scaledOutputTaps[L_DELAY_1_L_TAP_2]);
    leftOutDCBlock.input -= leftApf2.delay.tap(scaledOutputTaps[L_APF_2_L_TAP]);
    leftOutDCBlock.input += leftDelay2.tap(scaledOutputTaps[L_DELAY_2_L_TAP]);
    leftOutDCBlock.input -= rightDelay1.tap(scaledOutputTaps[R_DELAY_1_L_TAP]);
    leftOutDCBlock.input -= rightApf2.delay.tap(scaledOutputTaps[R_APF_2_L_TAP]);
    leftOutDCBlock.input -= rightDelay2.tap(scaledOutputTaps[R_DELAY_2_L_TAP]);

    rightOutDCBlock.input = rightApf1.output;
    rightOutDCBlock.input += rightDelay1.tap(scaledOutputTaps[R_DELAY_1_R_TAP_1]);
    rightOutDCBlock.input += rightDelay1.tap(scaledOutputTaps[R_DELAY_1_R_TAP_2]);
    rightOutDCBlock.input -= rightApf2.delay.tap(scaledOutputTaps[R_APF_2_R_TAP]);
    rightOutDCBlock.input += rightDelay2.tap(scaledOutputTaps[R_DELAY_2_R_TAP]);
    rightOutDCBlock.input -= leftDelay1.tap(scaledOutputTaps[L_DELAY_1_R_TAP]);
    rightOutDCBlock.input -= leftApf2.delay.tap(scaledOutputTaps[L_APF_2_R_TAP]);
    rightOutDCBlock.input -= leftDelay2.tap(scaledOutputTaps[L_DELAY_2_R_TAP]);

    *leftOut = leftOutDCBlock.process() * 0.5;
    *rightOut = rightOutDCBlock.process() * 0.5;

    fade += fadeStep * fadeDir;
    fade = (fade < 0.0) ? 0.0 : ((fade > 1.0) ? 1.0 : fade);

    assert(fade >= 0.0);
    assert(fade <= 1.0);
}

void Dattorro1997Tank::freeze(bool freezeFlag) {
    frozen = freezeFlag;
    if (frozen) {
        fadeDir = -1.0;
        decay = 1.0;
    }
    else {
        fadeDir = 1.0;
        decay = decayParam;
    }
}

void Dattorro1997Tank::setSampleRate(const FLOAT newSampleRate) {
    sampleRate = newSampleRate;
    sampleRate = sampleRate > maxSampleRate ? maxSampleRate : sampleRate;
    sampleRate = sampleRate < 1.f ? 1.f : sampleRate;
    sampleRateScale = sampleRate / dattorroSampleRate;

    fadeStep = 1.0 / sampleRate;

    leftOutDCBlock.setSampleRate(sampleRate);
    rightOutDCBlock.setSampleRate(sampleRate);

    rescaleTapTimes();
    setTimeScale(timeScale);
    initialiseDelaysAndApfs();
    clear();
}

void Dattorro1997Tank::setTimeScale(const FLOAT newTimeScale) {
    if (timeScale != newTimeScale) {
        timeScale = newTimeScale;
        timeScale = timeScale < minTimeScale ? minTimeScale : timeScale;

        rescaleApfAndDelayTimes();
    }
}

void Dattorro1997Tank::setDecay(const FLOAT newDecay) {
    decayParam = (FLOAT)(newDecay > 1.0 ? 1.0 :
                         (newDecay < 0.0 ? 0.0 : newDecay));
}

void Dattorro1997Tank::setModSpeed(const FLOAT newModSpeed) {
    lfo1.setFrequency(lfo1Freq * newModSpeed);
    lfo2.setFrequency(lfo2Freq * newModSpeed);
    lfo3.setFrequency(lfo3Freq * newModSpeed);
    lfo4.setFrequency(lfo4Freq * newModSpeed);
}

void Dattorro1997Tank::setModDepth(const FLOAT newModDepth) {
    modDepth = newModDepth;
    lfoExcursion = newModDepth * lfoMaxExcursion * sampleRateScale;
}

void Dattorro1997Tank::setModShape(const FLOAT shape) {
    lfo1.setRevPoint(shape);
    lfo2.setRevPoint(shape);
    lfo3.setRevPoint(shape);
    lfo4.setRevPoint(shape);
}

void Dattorro1997Tank::setHighCutFrequency(const FLOAT frequency) {
    leftHighCutFilter.setCutoffFreq(frequency);
    rightHighCutFilter.setCutoffFreq(frequency);
}

void Dattorro1997Tank::setLowCutFrequency(const FLOAT frequency) {
    leftLowCutFilter.setCutoffFreq(frequency);
    rightLowCutFilter.setCutoffFreq(frequency);
}

void Dattorro1997Tank::setDiffusion(const FLOAT diffusion) {
    assert(diffusion >= 0.0 && diffusion <= 10.0);

    FLOAT diffusion1 = scale(diffusion, FLOAT(0.0), FLOAT(10.0), FLOAT(0.0), maxDiffusion1);
    FLOAT diffusion2 = scale(diffusion, FLOAT(0.0), FLOAT(10.0), FLOAT(0.0), maxDiffusion2);

    leftApf1.setGain(-diffusion1);
    leftApf2.setGain(diffusion2);
    rightApf1.setGain(-diffusion1);
    rightApf2.setGain(diffusion2);
}


bool Dattorro1997Tank::clear_step(unsigned clear_block) {
    bool all_done = true;

    all_done &= leftApf1.clear_step(clear_block);
    all_done &= leftDelay1.clear_step(clear_block);
    leftHighCutFilter.clear();
    leftLowCutFilter.clear();
    all_done &= leftApf2.clear_step(clear_block);
    all_done &= leftDelay2.clear_step(clear_block);

    all_done &= rightApf1.clear_step(clear_block);
    all_done &= rightDelay1.clear_step(clear_block);
    rightHighCutFilter.clear();
    rightLowCutFilter.clear();
    all_done &= rightApf2.clear_step(clear_block);
    all_done &= rightDelay2.clear_step(clear_block);

    leftOutDCBlock.clear();
    rightOutDCBlock.clear();

    leftSum = 0.0;
    rightSum = 0.0;

    return all_done;
}

void Dattorro1997Tank::clear() {
    leftApf1.clear();
    leftDelay1.clear();
    leftHighCutFilter.clear();
    leftLowCutFilter.clear();
    leftApf2.clear();
    leftDelay2.clear();

    rightApf1.clear();
    rightDelay1.clear();
    rightHighCutFilter.clear();;
    rightLowCutFilter.clear();
    rightApf2.clear();
    rightDelay2.clear();

    leftOutDCBlock.clear();
    rightOutDCBlock.clear();

    leftSum = 0.0;
    rightSum = 0.0;
}

void Dattorro1997Tank::initialiseDelaysAndApfs() {
    auto maxScaledOutputTap = *std::max_element(scaledOutputTaps.begin(),
                                                scaledOutputTaps.end());
    auto calcMaxTime = [&](FLOAT delayTime) -> long {
        return (long)(sampleRateScale * (delayTime * maxTimeScale + 
                                         maxScaledOutputTap + timePadding));
    };

    const long kLeftApf1MaxTime = calcMaxTime(leftApf1Time);
    const long kLeftDelay1MaxTime = calcMaxTime(leftDelay1Time);
    const long kLeftApf2MaxTime = calcMaxTime(leftApf2Time);
    const long kLeftDelay2MaxTime = calcMaxTime(leftDelay2Time);
    const long kRightApf1MaxTime = calcMaxTime(rightApf1Time);
    const long kRightDelay1MaxTime = calcMaxTime(rightDelay1Time);
    const long kRightApf2MaxTime = calcMaxTime(rightApf2Time);
    const long kRightDelay2MaxTime = calcMaxTime(rightDelay2Time);

    leftApf1 = AllpassFilter<FLOAT>(kLeftApf1MaxTime);
    leftDelay1 = InterpDelay<FLOAT>(kLeftDelay1MaxTime);
    leftApf2 = AllpassFilter<FLOAT>(kLeftApf2MaxTime);
    leftDelay2 = InterpDelay<FLOAT>(kLeftDelay2MaxTime);
    rightApf1 = AllpassFilter<FLOAT>(kRightApf1MaxTime);
    rightDelay1 = InterpDelay<FLOAT>(kRightDelay1MaxTime);
    rightApf2 = AllpassFilter<FLOAT>(kRightApf2MaxTime);
    rightDelay2 = InterpDelay<FLOAT>(kRightDelay2MaxTime);
}

void Dattorro1997Tank::tickApfModulation() {
    auto l1 = lfo1.process();
    auto l2 = lfo2.process();
    auto l3 = lfo3.process();
    auto l4 = lfo4.process();

    leftApf1.delay.setDelayTime(l1 * lfoExcursion + scaledLeftApf1Time);
    leftApf2.delay.setDelayTime(l2 * lfoExcursion + scaledLeftApf2Time);
    rightApf1.delay.setDelayTime(l3 * lfoExcursion + scaledRightApf1Time);
    rightApf2.delay.setDelayTime(l4 * lfoExcursion + scaledRightApf2Time);
}

void Dattorro1997Tank::rescaleApfAndDelayTimes() {
    FLOAT scaleFactor = timeScale * sampleRateScale;

    scaledLeftApf1Time = leftApf1Time * scaleFactor;
    scaledLeftDelay1Time = leftDelay1Time * scaleFactor;
    scaledLeftApf2Time = leftApf2Time * scaleFactor;
    scaledLeftDelay2Time = leftDelay2Time * scaleFactor;

    scaledRightApf1Time = rightApf1Time * scaleFactor;
    scaledRightDelay1Time = rightDelay1Time * scaleFactor;
    scaledRightApf2Time = rightApf2Time * scaleFactor;
    scaledRightDelay2Time = rightDelay2Time * scaleFactor;

    leftDelay1.setDelayTime(scaledLeftDelay1Time);
    leftDelay2.setDelayTime(scaledLeftDelay2Time);
    rightDelay1.setDelayTime(scaledRightDelay1Time);
    rightDelay2.setDelayTime(scaledRightDelay2Time);
}

void Dattorro1997Tank::rescaleTapTimes() {
    for (size_t i = 0; i < scaledOutputTaps.size(); ++i) {
        scaledOutputTaps[i] = (long)((FLOAT)kOutputTaps[i] * sampleRateScale);
    }
}

Dattorro::Dattorro(const FLOAT initMaxSampleRate,
                   const FLOAT initMaxLfoDepth,
                   const FLOAT initMaxTimeScale)
    : tank(initMaxSampleRate, initMaxLfoDepth, initMaxTimeScale)
{
    sampleRate = initMaxSampleRate;
    dattorroScaleFactor = sampleRate / dattorroSampleRate;

    preDelay = InterpDelay<FLOAT>(192010, 0);

    inputLpf = OnePoleLPFilter(22000.0);
    inputHpf = OnePoleHPFilter(0.0);

    inApf1 = AllpassFilter<FLOAT>(dattorroScale(8 * kInApf1Time), dattorroScale(kInApf1Time), inputDiffusion1);
    inApf2 = AllpassFilter<FLOAT>(dattorroScale(8 * kInApf2Time), dattorroScale(kInApf2Time), inputDiffusion1);
    inApf3 = AllpassFilter<FLOAT>(dattorroScale(8 * kInApf3Time), dattorroScale(kInApf3Time), inputDiffusion2);
    inApf4 = AllpassFilter<FLOAT>(dattorroScale(8 * kInApf4Time), dattorroScale(kInApf4Time), inputDiffusion2);

    leftInputDCBlock.setCutoffFreq(20.0);
    rightInputDCBlock.setCutoffFreq(20.0);
}

void Dattorro::process(FLOAT leftInput, FLOAT rightInput) {
    leftInputDCBlock.input = leftInput;
    rightInputDCBlock.input = rightInput;
    inputLpf.setCutoffFreq(inputHighCut);
    inputHpf.setCutoffFreq(inputLowCut);
    inputLpf.input = leftInputDCBlock.process() + rightInputDCBlock.process();
    inputHpf.input = inputLpf.process();
    inputHpf.process();
    preDelay.input = inputHpf.output;
    preDelay.process();
    inApf1.input = preDelay.output;
    inApf2.input = inApf1.process();
    inApf3.input = inApf2.process();
    inApf4.input = inApf3.process();
    tankFeed = preDelay.output * (1.0 - diffuseInput) + inApf4.process() * diffuseInput;

    tank.process(tankFeed, tankFeed, &leftOut, &rightOut);
}


void Dattorro::clear_start() {
    clear_block_ctr = 0;
}

// Amortize clearing buffers over a few thousand sample frames
bool Dattorro::clear_step() {
    // Always keep these cleared: it's fast
    leftInputDCBlock.clear();
    rightInputDCBlock.clear();
    inputLpf.clear();
    inputHpf.clear();

    auto all_done = true;

    // Clear these largeer buffers in blocks
    all_done &= preDelay.clear_step(clear_block_ctr);
    all_done &= inApf1.clear_step(clear_block_ctr);
    all_done &= inApf2.clear_step(clear_block_ctr);
    all_done &= inApf3.clear_step(clear_block_ctr);
    all_done &= inApf4.clear_step(clear_block_ctr);
    all_done &= tank.clear_step(clear_block_ctr);

    clear_block_ctr++;

    return all_done;
}

void Dattorro::clear() {
    leftInputDCBlock.clear();
    rightInputDCBlock.clear();

    inputLpf.clear();
    inputHpf.clear();
    preDelay.clear();
    inApf1.clear();
    inApf2.clear();
    inApf3.clear();
    inApf4.clear();

    tank.clear();
}

void Dattorro::setTimeScale(FLOAT timeScale) {
    constexpr FLOAT minTimeScale = 0.0001;
    timeScale = timeScale < minTimeScale ? minTimeScale : timeScale;
    tank.setTimeScale(timeScale);
}

void Dattorro::setPreDelay(FLOAT t) {
    preDelayTime = t;
    preDelay.setDelayTime(preDelayTime * sampleRate);
}

void Dattorro::setSampleRate(FLOAT newSampleRate) {
    assert(newSampleRate > 0.);

    sampleRate = newSampleRate;
    tank.setSampleRate(sampleRate);
    dattorroScaleFactor = sampleRate / dattorroSampleRate;
    setPreDelay(preDelayTime);
    inApf1.delay.setDelayTime(dattorroScale(kInApf1Time));
    inApf2.delay.setDelayTime(dattorroScale(kInApf2Time));
    inApf3.delay.setDelayTime(dattorroScale(kInApf3Time));
    inApf4.delay.setDelayTime(dattorroScale(kInApf4Time));

    leftInputDCBlock.setSampleRate(sampleRate);
    rightInputDCBlock.setSampleRate(sampleRate);
    inputLpf.setSampleRate(sampleRate);
    inputHpf.setSampleRate(sampleRate);

    clear();
}

void Dattorro::freeze(bool freezeFlag) {
    tank.freeze(freezeFlag);
}

void Dattorro::setInputFilterLowCutoffPitch(FLOAT pitch) {
    inputLowCut = 440.0 * Pow2(pitch - 5.0);
}

void Dattorro::setInputFilterHighCutoffPitch(FLOAT pitch) {
    inputHighCut = 440.0 * Pow2(pitch - 5.0);
}

void Dattorro::enableInputDiffusion(bool enable) {
    diffuseInput = enable ? 1.0 : 0.0;
}

void Dattorro::setDecay(FLOAT newDecay) {
    decay = newDecay;
    assert(decay <= 1.0);
    tank.setDecay(decay);
}

void Dattorro::setTankDiffusion(const FLOAT diffusion) {
    tank.setDiffusion(diffusion);
}

void Dattorro::setTankFilterHighCutFrequency(const FLOAT pitch) {
    auto frequency = 440.0 * Pow2(pitch - 5.0);
    tank.setHighCutFrequency(frequency);
}

void Dattorro::setTankFilterLowCutFrequency(const FLOAT pitch) {
    auto frequency = 440.0 * Pow2(pitch - 5.0);
    tank.setLowCutFrequency(frequency);
}

void Dattorro::setTankModSpeed(const FLOAT modSpeed) {
    tank.setModSpeed(modSpeed);
}

void Dattorro::setTankModDepth(const FLOAT modDepth) {
    tank.setModDepth(modDepth);
}

void Dattorro::setTankModShape(const FLOAT modShape) {
    tank.setModShape(modShape);
}

FLOAT Dattorro::getLeftOutput() const {
    return leftOut;
}

FLOAT Dattorro::getRightOutput() const {
    return rightOut;
}

FLOAT Dattorro::dattorroScale(FLOAT delayTime) {
    return delayTime * dattorroScaleFactor;
}

