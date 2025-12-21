/*
  ==============================================================================

    SongRecognition.h
    Created: 19 Dec 2024
    Author: Explicitly Audio Systems

    Song identification using Chromaprint + AcoustID.
    Fetches lyrics from LyricsOVH for known songs.

  ==============================================================================
*/

#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <string>
#include <vector>

/**
    Song identification and lyrics fetching.
    
    Uses Chromaprint for audio fingerprinting and AcoustID for identification.
    Fetches lyrics from LyricsOVH API (free, no authentication required).
    Designed to reduce Whisper load by using pre-fetched lyrics.
*/
class SongRecognition
{
public:
    struct SongInfo
    {
        std::string artist;
        std::string title;
        std::string album;
        float confidence;       // 0.0-1.0
        std::string lyrics;     // Full lyrics (if fetched)
        bool identified;
    };
    
    SongRecognition();
    ~SongRecognition() = default;
    
    /**
        Initialize with Chromaprint path and AcoustID API key.
        
        @param fpcalcPath   Full path to fpcalc.exe (Chromaprint fingerprinter)
        @param apiKey       AcoustID API key (free from https://acoustid.org/api-key)
        @return             true if fpcalc exists and API key is valid
    */
    bool initialize(const std::string& fpcalcPath, const std::string& apiKey);
    
    /**
        Identify song from audio buffer.
        
        @param audioBuffer  Audio samples (mono, any sample rate)
        @param numSamples   Number of samples
        @param sampleRate   Sample rate in Hz
        @return             Song information (identified=true if found)
        
        Requires: 5-10 seconds of audio for reliable identification
        Processing time: ~500ms-2s (network latency)
    */
    SongInfo identifySong(const float* audioBuffer, int numSamples, double sampleRate);
    
    /**
        Fetch lyrics for identified song from LyricsOVH.
        
        Uses LyricsOVH API (free, no authentication required).
        
        @param artist       Artist name
        @param title        Song title
        @return             Lyrics text (empty if not found)
        
        Processing time: ~200ms-1s
    */
    std::string fetchLyrics(const std::string& artist, const std::string& title);
    
    /**
        Check if song recognition is enabled and configured.
    */
    bool isEnabled() const { return enabled_; }
    
    /**
        Enable/disable song recognition.
    */
    void setEnabled(bool enabled) { enabled_ = enabled; }
    
private:
    bool enabled_ = false;
    std::string fpcalcPath_;
    std::string acoustidApiKey_;
    
    // Chromaprint + AcoustID
    std::string createFingerprint(const float* audioBuffer, int numSamples, double sampleRate);
    std::string queryAcoustID(const std::string& fingerprint, int duration);
    SongInfo parseAcoustIDResponse(const std::string& response);
    
    // Lyrics fetching
    std::string fetchFromLyricsOVH(const std::string& artist, const std::string& title);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SongRecognition)
};
