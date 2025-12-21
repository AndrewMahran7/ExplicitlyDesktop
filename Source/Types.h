/*
  ==============================================================================

    Types.h
    Created: 9 Dec 2024
    Author: Explicitly Audio Systems

    Shared data structures for thread communication.

  ==============================================================================
*/

#pragma once

#include <cstdint>

/**
    Audio chunk metadata passed from Audio thread to ASR thread.
    
    Metadata-only: ~32 bytes (safe for stack copying in real-time callback)
    ASRThread reads actual audio data from CircularBuffer using this metadata.
*/
struct AudioChunk
{
    int64_t buffer_position;    // Absolute sample position in stream
    int num_samples;            // Number of samples available in CircularBuffer
    int num_channels;           // Number of channels in CircularBuffer
    double timestamp;           // Timestamp for latency tracking
};

/**
    Censorship event passed from ASR thread to Audio thread.
*/
struct CensorEvent
{
    enum class Mode
    {
        Reverse,
        Mute
    };
    
    int64_t start_sample;       // Absolute start position
    int64_t end_sample;         // Absolute end position
    Mode mode;                  // Censorship mode
    char word[64];              // Detected profanity word (for debugging)
    double confidence;          // ASR confidence (for debugging)
};

/**
    Debug message for UI display.
*/
struct DebugMessage
{
    enum class Type
    {
        ASRPartial,     // Partial ASR result
        ASRFinal,       // Final ASR result
        ProfanityDetected,  // Profanity word detected
        CensorApplied,  // Censorship applied to audio
        BufferStatus,   // Buffer position info
        RawJSON        // Raw Vosk JSON output
    };
    
    Type type;
    char text[512];
    int64_t timestamp_ms;
    int64_t start_sample;
    int64_t end_sample;
    double confidence;
    bool is_profanity;
};
