/*
  ==============================================================================

    CircularBuffer.h
    Created: 9 Dec 2024
    Author: Explicitly Audio Systems

    Lock-free circular buffer for ultra-low latency audio streaming.
    
    This implements a thread-safe circular buffer that:
    - Holds 150-300ms of audio samples
    - Supports lock-free concurrent read/write
    - Handles wraparound automatically
    - Real-time safe (no allocations in hot path)
    - Optimized for streaming ASR processing

  ==============================================================================
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <atomic>
#include <vector>

/**
    Lock-free circular buffer optimized for streaming real-time audio.
    
    Thread Safety Model:
    - Single writer (audio callback thread)
    - Single reader (ASR processing thread)
    - No locks, uses atomic operations for synchronization
    
    Memory Layout (300ms @ 48kHz stereo = 28,800 samples):
    [====================]
     ^read         ^write
     
    When write catches up to read, oldest data is overwritten (ring behavior).
    
    Performance Characteristics:
    - Write: O(1), lock-free, real-time safe
    - Read: O(n) where n = samples to read, lock-free
    - No dynamic allocation after construction
    - Cache-friendly linear memory access
*/
class CircularAudioBuffer
{
public:
    /**
        Create a circular buffer with specified capacity.
        
        @param num_channels     Number of audio channels (1=mono, 2=stereo)
        @param capacity_samples Total number of samples to store per channel
        
        Example for 300ms at 48kHz:
            CircularAudioBuffer(2, 14400)  // 0.3 * 48000 = 14.4k samples per channel
    */
    CircularAudioBuffer(int num_channels, int capacity_samples)
        : numChannels(num_channels)
        , capacitySamples(capacity_samples)
        , writePosition(0)
        , readPosition(0)
    {
        // Allocate buffer memory (150-300ms at 48kHz: 7,200-14,400 samples/channel)
        buffer.resize(numChannels);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            buffer[ch].resize(capacitySamples, 0.0f);
        }
        
        jassert(numChannels > 0);
        jassert(capacitySamples > 0);
        
        juce::Logger::writeToLog(juce::String("[CircularBuffer] Created: ") 
            + juce::String(numChannels) + " channels, " 
            + juce::String(capacitySamples) + " samples ("
            + juce::String(capacitySamples / 48000.0, 3) + " seconds @ 48kHz)");
    }

    /**
        Write audio samples to the buffer (called by audio thread).
        
        This is lock-free and real-time safe. If the buffer is full,
        it wraps around and overwrites the oldest data.
        
        @param source           Source audio buffer to copy from
        @param num_samples      Number of samples to write
        
        Thread: Audio callback (real-time critical)
    */
    void writeSamples(const juce::AudioBuffer<float>& source, int num_samples)
    {
        // Safety check: ensure channel count matches
        if (source.getNumChannels() != numChannels || num_samples > capacitySamples || num_samples < 0)
            return;
        
        const int write_pos = writePosition.load(std::memory_order_acquire);
        
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* src = source.getReadPointer(ch);
            if (src == nullptr)
            {
                juce::Logger::writeToLog("[CircularBuffer] ERROR: Null source pointer for channel " + juce::String(ch));
                continue;
            }
            
            for (int i = 0; i < num_samples; ++i)
            {
                const int idx = (write_pos + i) % capacitySamples;
                buffer[ch][idx] = src[i];
            }
        }
        
        // Update write position atomically
        const int new_write_pos = (write_pos + num_samples) % capacitySamples;
        writePosition.store(new_write_pos, std::memory_order_release);
    }

    /**
        Read the last N seconds of audio from the buffer (called by ML thread).
        
        This copies data to avoid race conditions with the writer.
        Returns audio starting from (current_write_pos - duration_samples).
        
        @param output           Destination buffer to copy to
        @param duration_seconds How many seconds of audio to read
        @param sample_rate      Current sample rate (to convert seconds to samples)
        @return                 Actual number of samples read
        
        Thread: ML processing thread (background)
    */
    int readLastNSeconds(juce::AudioBuffer<float>& output, double duration_seconds, double sample_rate)
    {
        const int samples_requested = static_cast<int>(duration_seconds * sample_rate);
        const int samples_to_read = juce::jmin(samples_requested, capacitySamples);
        
        // Ensure output buffer is sized correctly
        output.setSize(numChannels, samples_to_read, false, false, true);
        
        const int write_pos = writePosition.load(std::memory_order_acquire);
        
        // Calculate start position (going backward from write position)
        int start_pos = write_pos - samples_to_read;
        if (start_pos < 0)
            start_pos += capacitySamples;
        
        // Copy samples handling wraparound
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* dest = output.getWritePointer(ch);
            
            if (start_pos + samples_to_read <= capacitySamples)
            {
                // Simple case: no wraparound
                std::memcpy(dest, buffer[ch].data() + start_pos, samples_to_read * sizeof(float));
            }
            else
            {
                // Wraparound case: copy in two parts
                const int samples_until_end = capacitySamples - start_pos;
                const int samples_from_start = samples_to_read - samples_until_end;
                
                std::memcpy(dest, buffer[ch].data() + start_pos, samples_until_end * sizeof(float));
                std::memcpy(dest + samples_until_end, buffer[ch].data(), samples_from_start * sizeof(float));
            }
        }
        
        return samples_to_read;
    }

    /**
        Read samples from specific buffer position (for ASRThread processing).
        
        Used by ASRThread to read audio data using metadata from queue.
        Handles wraparound automatically.
        
        @param output           Destination buffer to copy samples to
        @param start_position   Starting absolute sample position
        @param num_samples      Number of samples to read
        @return                 True if successful
        
        Thread: ASR processing thread (background)
    */
    bool readSamplesAt(juce::AudioBuffer<float>& output, int64_t start_position, int num_samples)
    {
        if (num_samples <= 0 || num_samples > capacitySamples)
            return false;
        
        // Ensure we have valid data to read (avoid reading uninitialized memory)
        // Buffer must be initialized with zeros in constructor, so this is just a sanity check
        if (start_position < 0)
            return false;
        
        try
        {
            output.setSize(numChannels, num_samples, false, false, true);
        }
        catch (...)
        {
            return false;
        }
        
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* dest = output.getWritePointer(ch);
            if (!dest)
                return false;
            
            for (int i = 0; i < num_samples; ++i)
            {
                const int idx = static_cast<int>((start_position + i) % capacitySamples);
                dest[i] = buffer[ch][idx];
            }
        }
        
        return true;
    }

    /**
        Get sample at specific absolute position (for timestamp-based access).
        
        This is used by the audio thread to apply censorship at specific timestamps.
        
        @param channel          Channel index
        @param absolute_sample  Absolute sample position (not wrapped)
        @return                 Sample value at that position
        
        Thread: Audio callback (real-time critical)
    */
    float getSampleAt(int channel, int64_t absolute_sample) const
    {
        jassert(channel >= 0 && channel < numChannels);
        
        const int idx = static_cast<int>(absolute_sample % capacitySamples);
        return buffer[channel][idx];
    }

    /**
        Set sample at specific absolute position (for in-place censorship).
        
        Used to apply mute/reverse effects directly in the circular buffer.
        
        @param channel          Channel index
        @param absolute_sample  Absolute sample position (not wrapped)
        @param value            New sample value
        
        Thread: Audio callback (real-time critical)
    */
    void setSampleAt(int channel, int64_t absolute_sample, float value)
    {
        jassert(channel >= 0 && channel < numChannels);
        
        const int idx = static_cast<int>(absolute_sample % capacitySamples);
        buffer[channel][idx] = value;
    }

    /**
        Get current write position (total samples written since start).
        
        @return Absolute write position
    */
    int64_t getWritePosition() const
    {
        return writePosition.load(std::memory_order_acquire);
    }

    /**
        Get buffer capacity in samples.
        
        @return Total capacity per channel
    */
    int getCapacity() const
    {
        return capacitySamples;
    }

    /**
        Get number of channels.
        
        @return Channel count
    */
    int getNumChannels() const
    {
        return numChannels;
    }

    /**
        Reset the buffer (clear all audio data).
        
        WARNING: Not thread-safe! Only call when audio processing is stopped.
    */
    void reset()
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            std::fill(buffer[ch].begin(), buffer[ch].end(), 0.0f);
        }
        
        writePosition.store(0, std::memory_order_release);
        readPosition.store(0, std::memory_order_release);
        
        juce::Logger::writeToLog("[CircularBuffer] Reset complete");
    }

private:
    int numChannels;
    int capacitySamples;
    
    // Lock-free atomic positions
    std::atomic<int> writePosition;  // Current write position (wraps at capacity)
    std::atomic<int> readPosition;   // Last read position (for diagnostics)
    
    // Audio data storage [channel][sample]
    std::vector<std::vector<float>> buffer;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CircularAudioBuffer)
};
