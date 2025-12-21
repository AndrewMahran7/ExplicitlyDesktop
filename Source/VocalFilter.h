/*
  ==============================================================================

    VocalFilter.h
    Created: 10 Dec 2024
    Author: Explicitly Audio Systems

    Lightweight vocal frequency isolation using bandpass filtering.
    Isolates 300 Hz - 3 kHz range where human vocals are strongest.

  ==============================================================================
*/

#pragma once

#include <vector>
#include <cmath>

/**
    Lightweight vocal isolation filter using bandpass filtering.
    
    Isolates human vocal frequencies (300 Hz - 3 kHz) to improve
    speech transcription accuracy on music. Much faster than
    source separation models like Demucs.
    
    Uses 2nd-order Butterworth filters for minimal CPU usage.
*/
class VocalFilter
{
public:
    VocalFilter();
    ~VocalFilter() = default;
    
    /**
        Initialize filter for given sample rate.
        
        @param sampleRate   Audio sample rate (e.g., 48000)
    */
    void initialize(double sampleRate);
    
    /**
        Process audio buffer to isolate vocal frequencies.
        
        Applies bandpass filter (300 Hz - 3 kHz) in-place.
        
        @param buffer   Audio samples to process
    */
    void processBuffer(std::vector<float>& buffer);
    
    /**
        Reset filter state (clear history).
    */
    void reset();
    
private:
    // Biquad filter coefficients
    struct BiquadCoeffs
    {
        double b0, b1, b2;  // Numerator coefficients
        double a1, a2;      // Denominator coefficients
    };
    
    // Filter state (history)
    struct BiquadState
    {
        double x1 = 0.0, x2 = 0.0;  // Input history
        double y1 = 0.0, y2 = 0.0;  // Output history
    };
    
    /**
        Calculate high-pass filter coefficients (removes low frequencies).
        
        @param cutoffHz     Cutoff frequency in Hz
        @param sampleRate   Sample rate in Hz
        @return             Biquad coefficients
    */
    BiquadCoeffs calculateHighPass(double cutoffHz, double sampleRate);
    
    /**
        Calculate low-pass filter coefficients (removes high frequencies).
        
        @param cutoffHz     Cutoff frequency in Hz
        @param sampleRate   Sample rate in Hz
        @return             Biquad coefficients
    */
    BiquadCoeffs calculateLowPass(double cutoffHz, double sampleRate);
    
    /**
        Apply biquad filter to single sample.
        
        @param sample   Input sample
        @param coeffs   Filter coefficients
        @param state    Filter state (updated in-place)
        @return         Filtered output sample
    */
    double processSample(double sample, const BiquadCoeffs& coeffs, BiquadState& state);
    
    double sampleRate_;
    
    // High-pass filter (removes < 300 Hz: bass, kick drum)
    BiquadCoeffs highPassCoeffs_;
    BiquadState highPassState_;
    
    // Low-pass filter (removes > 3000 Hz: cymbals, hi-hats)
    BiquadCoeffs lowPassCoeffs_;
    BiquadState lowPassState_;
    
    bool initialized_ = false;
};
