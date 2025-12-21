/*
  ==============================================================================

    ASRThread.h
    Created: 9 Dec 2024
    Author: Explicitly Audio Systems

    Vosk streaming ASR thread for real-time profanity detection.
    
    Responsibilities:
    - Initialize Vosk model and recognizer
    - Receive audio chunks from lock-free queue
    - Feed audio to Vosk streaming API
    - Parse JSON results for word tokens + timestamps
    - Detect profanity via ProfanityFilter
    - Send censorship events to audio thread
    
    Thread: Background (not real-time critical)

  ==============================================================================
*/

#pragma once

#include <juce_core/juce_core.h>
#include "LockFreeQueue.h"
#include "ProfanityFilter.h"
#include "Types.h"
#include <vosk_api.h>
#include <atomic>
#include <thread>
#include <memory>

class CircularAudioBuffer;

class ASRThread
{
public:
    explicit ASRThread(int sampleRate);
    ~ASRThread();
    
    /**
        Start the ASR processing thread.
        
        @param audioQueue       Queue to receive audio chunk metadata from
        @param censorQueue      Queue to send censorship events to
        @param circBuffer       Circular buffer to read audio data from
        @return                 true if started successfully
    */
    bool start(LockFreeQueue<AudioChunk, 64>* audioQueue,
               LockFreeQueue<CensorEvent, 256>* censorQueue,
               CircularAudioBuffer* circBuffer);
    
    /**
        Stop the ASR processing thread.
    */
    void stop();
    
    /**
        Check if thread is running.
    */
    bool isRunning() const { return running.load(); }
    
    /**
        Get last error message.
    */
    juce::String getLastError() const { return lastError; }
    
    /**
        Set debug callback for UI updates.
    */
    void setDebugCallback(std::function<void(const juce::String&)> callback)
    {
        debugCallback = callback;
    }

private:
    void run();
    void processAudioChunk(const AudioChunk& chunk);
    void parseVoskResult(const char* json, int64_t bufferPosition);
    
    int sampleRate;
    
    VoskModel* voskModel = nullptr;
    VoskRecognizer* voskRecognizer = nullptr;
    
    ProfanityFilter profanityFilter;
    
    LockFreeQueue<AudioChunk, 64>* audioQueue = nullptr;
    LockFreeQueue<CensorEvent, 256>* censorQueue = nullptr;
    CircularAudioBuffer* circularBuffer = nullptr;
    
    std::unique_ptr<std::thread> processingThread;
    std::atomic<bool> running {false};
    juce::String lastError;
    std::function<void(const juce::String&)> debugCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ASRThread)
};
