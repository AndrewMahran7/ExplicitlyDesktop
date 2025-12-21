/*
  ==============================================================================

    VocalFilter.cpp
    Created: 10 Dec 2024
    Author: Explicitly Audio Systems

    Implementation of lightweight vocal frequency isolation.

  ==============================================================================
*/

#include "VocalFilter.h"
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

VocalFilter::VocalFilter()
{
}

void VocalFilter::initialize(double sampleRate)
{
    sampleRate_ = sampleRate;
    
    // Calculate filter coefficients for bandpass (150 Hz - 5000 Hz)
    // Extra wide range to minimize static while still isolating vocals
    highPassCoeffs_ = calculateHighPass(150.0, sampleRate);
    lowPassCoeffs_ = calculateLowPass(5000.0, sampleRate);
    
    reset();
    initialized_ = true;
    
    std::cout << "[VocalFilter] Initialized: " << sampleRate << " Hz, bandpass 150-5000 Hz" << std::endl;
}

void VocalFilter::reset()
{
    highPassState_ = BiquadState();
    lowPassState_ = BiquadState();
}

VocalFilter::BiquadCoeffs VocalFilter::calculateHighPass(double cutoffHz, double sampleRate)
{
    // 2nd-order Butterworth high-pass filter
    double omega = 2.0 * M_PI * cutoffHz / sampleRate;
    double cosOmega = std::cos(omega);
    double sinOmega = std::sin(omega);
    double alpha = sinOmega / (2.0 * 0.4);  // Q = 0.4 for very gentle rolloff (minimal artifacts)
    
    BiquadCoeffs coeffs;
    
    double a0 = 1.0 + alpha;
    coeffs.b0 = (1.0 + cosOmega) / 2.0 / a0;
    coeffs.b1 = -(1.0 + cosOmega) / a0;
    coeffs.b2 = (1.0 + cosOmega) / 2.0 / a0;
    coeffs.a1 = -2.0 * cosOmega / a0;
    coeffs.a2 = (1.0 - alpha) / a0;
    
    return coeffs;
}

VocalFilter::BiquadCoeffs VocalFilter::calculateLowPass(double cutoffHz, double sampleRate)
{
    // 2nd-order Butterworth low-pass filter
    double omega = 2.0 * M_PI * cutoffHz / sampleRate;
    double cosOmega = std::cos(omega);
    double sinOmega = std::sin(omega);
    double alpha = sinOmega / (2.0 * 0.4);  // Q = 0.4 for very gentle rolloff (minimal artifacts)
    
    BiquadCoeffs coeffs;
    
    double a0 = 1.0 + alpha;
    coeffs.b0 = (1.0 - cosOmega) / 2.0 / a0;
    coeffs.b1 = (1.0 - cosOmega) / a0;
    coeffs.b2 = (1.0 - cosOmega) / 2.0 / a0;
    coeffs.a1 = -2.0 * cosOmega / a0;
    coeffs.a2 = (1.0 - alpha) / a0;
    
    return coeffs;
}

double VocalFilter::processSample(double sample, const BiquadCoeffs& coeffs, BiquadState& state)
{
    // Direct Form II biquad implementation
    double output = coeffs.b0 * sample + coeffs.b1 * state.x1 + coeffs.b2 * state.x2
                  - coeffs.a1 * state.y1 - coeffs.a2 * state.y2;
    
    // Update state
    state.x2 = state.x1;
    state.x1 = sample;
    state.y2 = state.y1;
    state.y1 = output;
    
    return output;
}

void VocalFilter::processBuffer(std::vector<float>& buffer)
{
    if (!initialized_)
    {
        std::cerr << "[VocalFilter] Error: Filter not initialized!" << std::endl;
        return;
    }
    
    for (size_t i = 0; i < buffer.size(); ++i)
    {
        double sample = buffer[i];
        
        // Apply high-pass (remove bass/drums)
        sample = processSample(sample, highPassCoeffs_, highPassState_);
        
        // Apply low-pass (remove cymbals/hi-hats)
        sample = processSample(sample, lowPassCoeffs_, lowPassState_);
        
        buffer[i] = static_cast<float>(sample);
    }
}
