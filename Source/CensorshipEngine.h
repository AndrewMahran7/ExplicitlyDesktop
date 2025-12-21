/*
  ==============================================================================

    CensorshipEngine.h
    Created: 9 Dec 2024
    Author: Explicitly Audio Systems

    DSP for profanity censorship (reverse/mute with fade).
    
    Features:
    - Reverse samples (profanity played backwards)
    - Mute samples (silence with fade in/out)
    - 3-5ms fade to prevent clicks/pops
    - Real-time safe (no allocations)

  ==============================================================================
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <cmath>

/**
    Censorship DSP engine.
    
    Usage:
        CensorshipEngine engine;
        
        // Reverse profanity from sample 1000 to 2000
        engine.reverseSamples(audio_buffer, 1000, 2000, 48000);
        
        // Or mute it
        engine.muteSamples(audio_buffer, 1000, 2000, 48000);
*/
class CensorshipEngine
{
public:
    enum class CensorMode
    {
        Reverse,
        Mute
    };
    
    CensorshipEngine()
    {
    }
    
    /**
        Reverse audio samples (profanity played backwards).
        
        Applies fade in/out to prevent clicks.
        
        @param buffer           Audio buffer to modify in-place
        @param start_sample     Start position (sample index)
        @param end_sample       End position (sample index, exclusive)
        @param sample_rate      Sample rate in Hz
        
        Thread: Audio thread (real-time safe)
    */
    void reverseSamples(juce::AudioBuffer<float>& buffer, 
                       int64_t start_sample, 
                       int64_t end_sample,
                       int sample_rate)
    {
        const int64_t length = end_sample - start_sample;
        if (length <= 0) return;
        
        const int fade_samples = calculateFadeSamples(sample_rate);
        const int num_channels = buffer.getNumChannels();
        
        // Reverse samples for each channel
        for (int ch = 0; ch < num_channels; ++ch)
        {
            float* channel_data = buffer.getWritePointer(ch);
            
            // Reverse the samples
            for (int64_t i = 0; i < length / 2; ++i)
            {
                const int64_t left_idx = start_sample + i;
                const int64_t right_idx = end_sample - 1 - i;
                
                if (left_idx >= 0 && right_idx < buffer.getNumSamples() &&
                    left_idx < buffer.getNumSamples() && right_idx >= 0)
                {
                    std::swap(channel_data[left_idx], channel_data[right_idx]);
                }
            }
        }
        
        // Apply fade in/out to prevent clicks
        applyFadeIn(buffer, start_sample, fade_samples);
        applyFadeOut(buffer, end_sample - fade_samples, fade_samples);
    }
    
    /**
        Mute audio samples (silence with fade).
        
        @param buffer           Audio buffer to modify in-place
        @param start_sample     Start position (sample index)
        @param end_sample       End position (sample index, exclusive)
        @param sample_rate      Sample rate in Hz
        
        Thread: Audio thread (real-time safe)
    */
    void muteSamples(juce::AudioBuffer<float>& buffer,
                    int64_t start_sample,
                    int64_t end_sample,
                    int sample_rate)
    {
        const int64_t length = end_sample - start_sample;
        if (length <= 0) return;
        
        const int fade_samples = calculateFadeSamples(sample_rate);
        const int num_channels = buffer.getNumChannels();
        
        // Fade out at start
        applyFadeOut(buffer, start_sample, fade_samples);
        
        // Zero the middle section
        const int64_t zero_start = start_sample + fade_samples;
        const int64_t zero_end = end_sample - fade_samples;
        
        if (zero_end > zero_start)
        {
            for (int ch = 0; ch < num_channels; ++ch)
            {
                float* channel_data = buffer.getWritePointer(ch);
                
                for (int64_t i = zero_start; i < zero_end && i < buffer.getNumSamples(); ++i)
                {
                    channel_data[i] = 0.0f;
                }
            }
        }
        
        // Fade in at end
        applyFadeIn(buffer, end_sample - fade_samples, fade_samples);
    }
    
    /**
        Apply censorship based on mode.
        
        @param buffer       Audio buffer
        @param start        Start sample
        @param end          End sample
        @param mode         Reverse or Mute
        @param sample_rate  Sample rate in Hz
    */
    void applyCensorship(juce::AudioBuffer<float>& buffer,
                        int64_t start,
                        int64_t end,
                        CensorMode mode,
                        int sample_rate)
    {
        if (mode == CensorMode::Reverse)
        {
            reverseSamples(buffer, start, end, sample_rate);
        }
        else if (mode == CensorMode::Mute)
        {
            muteSamples(buffer, start, end, sample_rate);
        }
    }
    
private:
    /**
        Calculate fade duration in samples (3-5ms).
        
        @param sample_rate  Sample rate in Hz
        @return             Number of samples for fade
    */
    int calculateFadeSamples(int sample_rate) const
    {
        const float fade_duration_ms = 5.0f;  // 5 milliseconds
        return static_cast<int>(sample_rate * fade_duration_ms / 1000.0f);
    }
    
    /**
        Apply linear fade in.
        
        @param buffer       Audio buffer
        @param start        Start sample
        @param length       Fade length in samples
    */
    void applyFadeIn(juce::AudioBuffer<float>& buffer, int64_t start, int length)
    {
        const int num_channels = buffer.getNumChannels();
        
        for (int ch = 0; ch < num_channels; ++ch)
        {
            float* channel_data = buffer.getWritePointer(ch);
            
            for (int i = 0; i < length; ++i)
            {
                const int64_t idx = start + i;
                if (idx >= 0 && idx < buffer.getNumSamples())
                {
                    const float gain = static_cast<float>(i) / length;  // 0.0 to 1.0
                    channel_data[idx] *= gain;
                }
            }
        }
    }
    
    /**
        Apply linear fade out.
        
        @param buffer       Audio buffer
        @param start        Start sample
        @param length       Fade length in samples
    */
    void applyFadeOut(juce::AudioBuffer<float>& buffer, int64_t start, int length)
    {
        const int num_channels = buffer.getNumChannels();
        
        for (int ch = 0; ch < num_channels; ++ch)
        {
            float* channel_data = buffer.getWritePointer(ch);
            
            for (int i = 0; i < length; ++i)
            {
                const int64_t idx = start + i;
                if (idx >= 0 && idx < buffer.getNumSamples())
                {
                    const float gain = 1.0f - (static_cast<float>(i) / length);  // 1.0 to 0.0
                    channel_data[idx] *= gain;
                }
            }
        }
    }
};
