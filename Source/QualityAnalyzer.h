/*
  ==============================================================================

    QualityAnalyzer.h
    Created: 10 Dec 2024
    Author: Explicitly Audio Systems

    PHASE 8: Quality Analysis
    
    Tracks and analyzes:
    - Censorship statistics (words detected, censored, timing)
    - Audio quality metrics (levels, clipping)
    - Performance metrics (RTF, buffer health)
    - Real-time quality scoring

  ==============================================================================
*/

#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <fstream>
#include <mutex>

struct CensorshipEvent
{
    std::string word;
    double timestamp;           // When the word occurred (in audio time)
    double detectionTime;       // When we detected it (processing time)
    double detectionLatency;    // How long detection took
    bool wasCensored;          // false if skipped due to underrun
    std::string mode;          // "REVERSE" or "MUTE"
};

struct QualityMetrics
{
    // Censorship statistics
    int totalWordsDetected = 0;
    int totalWordsCensored = 0;
    int totalWordsSkipped = 0;      // Due to underrun
    int multiWordDetections = 0;     // "nig ga" style detections
    
    // Performance statistics
    double averageRTF = 0.0;
    double minRTF = 999.0;
    double maxRTF = 0.0;
    int rtfSamples = 0;
    
    // Buffer health
    double averageBufferSize = 0.0;
    double minBufferSize = 999.0;
    double maxBufferSize = 0.0;
    int bufferUnderrunCount = 0;
    int bufferSamples = 0;
    
    // Audio quality
    double peakLevel = 0.0;
    int clippingEvents = 0;
    
    // Session info
    double sessionDuration = 0.0;   // Total audio processed (seconds)
    std::chrono::steady_clock::time_point sessionStart;
};

class QualityAnalyzer
{
public:
    QualityAnalyzer();
    ~QualityAnalyzer();
    
    // Session management
    void startSession();
    void endSession();
    void reset();
    
    // Event tracking
    void recordCensorshipEvent(const std::string& word, double timestamp, 
                              bool wasCensored, const std::string& mode,
                              bool isMultiWord = false);
    void recordRTF(double rtf);
    void recordBufferSize(double bufferSize);
    void recordBufferUnderrun();
    void recordAudioLevel(float level);
    void recordClipping();
    void updateSessionDuration(double seconds);
    
    // Metrics retrieval
    QualityMetrics getMetrics() const;
    int getCensoredWordCount() const;
    int getSkippedWordCount() const;
    double getAverageRTF() const;
    double getCurrentQualityScore() const;  // 0-100 score
    std::vector<CensorshipEvent> getRecentEvents(int maxCount = 10) const;
    
    // Reporting
    std::string generateReport() const;
    bool exportToFile(const std::string& filename) const;
    
private:
    mutable std::mutex metricsMutex;
    QualityMetrics metrics;
    std::vector<CensorshipEvent> censorshipHistory;
    
    static constexpr int MAX_HISTORY_SIZE = 1000;  // Keep last 1000 events
    
    double calculateQualityScore() const;
};
