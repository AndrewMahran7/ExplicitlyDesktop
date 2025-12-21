/*
  ==============================================================================

    TimestampRefiner.h
    Created: 10 Dec 2024
    Author: Explicitly Audio Systems

    Refines Whisper timestamps using audio energy analysis.
    Fixes the issue where all words appear at 0.00s-0.10s.

  ==============================================================================
*/

#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include "LyricsAlignment.h"  // For WordSegment

class TimestampRefiner
{
public:
    TimestampRefiner() = default;
    
    // Refine a word's timestamp using audio energy analysis
    // audio: full audio buffer (48kHz)
    // word: word segment to refine (timestamps in seconds)
    // sampleRate: audio sample rate
    void refineWordTimestamp(WordSegment& word, 
                            const std::vector<float>& audio,
                            int sampleRate);
    
    // Find speech regions in audio buffer using energy + zero-crossing
    // Returns vector of (start, end) time pairs in seconds
    std::vector<std::pair<double, double>> findSpeechRegions(
        const std::vector<float>& audio,
        int sampleRate);

private:
    // Calculate RMS energy for a window of audio
    float calculateEnergy(const std::vector<float>& audio, 
                         int start, int length);
    
    // Calculate zero-crossing rate (detects voiced speech)
    float calculateZeroCrossing(const std::vector<float>& audio,
                               int start, int length);
    
    // Find the best word boundary using energy profile
    double findBestBoundary(const std::vector<float>& audio,
                           int centerSample,
                           int searchRadius,
                           int sampleRate,
                           bool findStart);
    
    // Search for actual speech content around Whisper's timestamp
    std::pair<double, double> searchForSpeech(
        const std::vector<float>& audio,
        double whisperStart,
        double whisperEnd,
        int sampleRate);
    
    // Parameters (tuned for music with vocals + tiny model late timestamps)
    static constexpr float ENERGY_THRESHOLD = 0.001f;     // Minimum energy for speech (lowered for music)
    static constexpr float ZC_THRESHOLD = 0.1f;           // Zero-crossing rate threshold (lowered)
    static constexpr int WINDOW_SIZE = 480;               // 10ms @ 48kHz
    static constexpr int SEARCH_RADIUS = 38400;           // 0.8s search radius @ 48kHz (increased for tiny model)
    static constexpr float MIN_WORD_DURATION = 0.05f;     // 50ms minimum word length
    static constexpr float MAX_WORD_DURATION = 2.0f;      // 2s maximum word length
};
