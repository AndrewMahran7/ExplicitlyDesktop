/*
  ==============================================================================

    WhisperThread.h
    Created: 9 Dec 2024
    Author: Explicitly Audio Systems

    Whisper.cpp ASR thread implementation.

  ==============================================================================
*/

#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <whisper.h>

#define WHISPER_SAMPLE_RATE 16000
#include <thread>
#include <atomic>
#include <functional>
#include "Types.h"
#include "LockFreeQueue.h"
#include "ProfanityFilter.h"

class CircularAudioBuffer;

/**
    Background thread for Whisper ASR processing.
    
    Continuously reads audio chunks from queue, processes with Whisper,
    detects profanity, and sends censorship events back.
*/
class WhisperThread
{
public:
    WhisperThread(int sampleRate);
    ~WhisperThread();
    
    // Start/stop processing
    bool start(LockFreeQueue<AudioChunk, 64>* audioQueue,
              LockFreeQueue<CensorEvent, 256>* censorQueue,
              CircularAudioBuffer* circularBuffer);
    void stop();
    
    // Get last error message
    juce::String getLastError() const { return lastError; }
    
    // Set debug callback for logging
    void setDebugCallback(std::function<void(const juce::String&)> callback)
    {
        debugCallback = callback;
    }

private:
    // Processing thread entry point
    void run();
    
    // Process single audio chunk
    void processAudioChunk(const AudioChunk& chunk);
    
    // Parse Whisper results and detect profanity
    void processTranscript(const char* text, int64_t bufferPosition);
    
    // Resample audio from native sample rate to 16kHz (Whisper requirement)
    std::vector<float> resampleTo16kHz(const std::vector<float>& input);
    
    // Whisper context
    whisper_context* whisperCtx = nullptr;
    whisper_full_params whisperParams;
    
    // Profanity detection
    ProfanityFilter profanityFilter;
    
    // Thread communication
    std::unique_ptr<std::thread> processingThread;
    std::atomic<bool> running{false};
    
    LockFreeQueue<AudioChunk, 64>* audioQueue = nullptr;
    LockFreeQueue<CensorEvent, 256>* censorQueue = nullptr;
    CircularAudioBuffer* circularBuffer = nullptr;
    
    // Audio processing buffer
    std::vector<float> audioBuffer;
    
    // Configuration
    int sampleRate;
    juce::String lastError;
    
    // Debug callback
    std::function<void(const juce::String&)> debugCallback;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WhisperThread)
};
