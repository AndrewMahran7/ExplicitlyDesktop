/*
  ==============================================================================

    QualityAnalyzer.cpp
    Created: 10 Dec 2024
    Author: Explicitly Audio Systems

    PHASE 8: Quality Analysis Implementation

  ==============================================================================
*/

#include "QualityAnalyzer.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>

QualityAnalyzer::QualityAnalyzer()
{
    std::cout << "[Phase8] Quality Analyzer initialized" << std::endl;
}

QualityAnalyzer::~QualityAnalyzer()
{
}

void QualityAnalyzer::startSession()
{
    std::lock_guard<std::mutex> lock(metricsMutex);
    metrics.sessionStart = std::chrono::steady_clock::now();
    std::cout << "[Phase8] Analysis session started" << std::endl;
}

void QualityAnalyzer::endSession()
{
    std::lock_guard<std::mutex> lock(metricsMutex);
    std::cout << "[Phase8] Analysis session ended" << std::endl;
    std::cout << "[Phase8] Total words censored: " << metrics.totalWordsCensored << std::endl;
    std::cout << "[Phase8] Quality score: " << calculateQualityScore() << "/100" << std::endl;
}

void QualityAnalyzer::reset()
{
    std::lock_guard<std::mutex> lock(metricsMutex);
    metrics = QualityMetrics();
    censorshipHistory.clear();
    std::cout << "[Phase8] Metrics reset" << std::endl;
}

void QualityAnalyzer::recordCensorshipEvent(const std::string& word, double timestamp,
                                           bool wasCensored, const std::string& mode,
                                           bool isMultiWord)
{
    std::lock_guard<std::mutex> lock(metricsMutex);
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - metrics.sessionStart).count() / 1000.0;
    
    CensorshipEvent event;
    event.word = word;
    event.timestamp = timestamp;
    event.detectionTime = elapsed;
    event.detectionLatency = elapsed - timestamp;
    event.wasCensored = wasCensored;
    event.mode = mode;
    
    censorshipHistory.push_back(event);
    
    // Keep history size manageable
    if (censorshipHistory.size() > MAX_HISTORY_SIZE)
    {
        censorshipHistory.erase(censorshipHistory.begin());
    }
    
    metrics.totalWordsDetected++;
    
    if (wasCensored)
    {
        metrics.totalWordsCensored++;
    }
    else
    {
        metrics.totalWordsSkipped++;
    }
    
    if (isMultiWord)
    {
        metrics.multiWordDetections++;
    }
}

void QualityAnalyzer::recordRTF(double rtf)
{
    std::lock_guard<std::mutex> lock(metricsMutex);
    
    metrics.rtfSamples++;
    metrics.averageRTF = ((metrics.averageRTF * (metrics.rtfSamples - 1)) + rtf) / metrics.rtfSamples;
    metrics.minRTF = std::min(metrics.minRTF, rtf);
    metrics.maxRTF = std::max(metrics.maxRTF, rtf);
}

void QualityAnalyzer::recordBufferSize(double bufferSize)
{
    std::lock_guard<std::mutex> lock(metricsMutex);
    
    metrics.bufferSamples++;
    metrics.averageBufferSize = ((metrics.averageBufferSize * (metrics.bufferSamples - 1)) + bufferSize) / metrics.bufferSamples;
    metrics.minBufferSize = std::min(metrics.minBufferSize, bufferSize);
    metrics.maxBufferSize = std::max(metrics.maxBufferSize, bufferSize);
}

void QualityAnalyzer::recordBufferUnderrun()
{
    std::lock_guard<std::mutex> lock(metricsMutex);
    metrics.bufferUnderrunCount++;
}

void QualityAnalyzer::recordAudioLevel(float level)
{
    std::lock_guard<std::mutex> lock(metricsMutex);
    metrics.peakLevel = std::max(metrics.peakLevel, (double)std::abs(level));
}

void QualityAnalyzer::recordClipping()
{
    std::lock_guard<std::mutex> lock(metricsMutex);
    metrics.clippingEvents++;
}

void QualityAnalyzer::updateSessionDuration(double seconds)
{
    std::lock_guard<std::mutex> lock(metricsMutex);
    metrics.sessionDuration = seconds;
}

QualityMetrics QualityAnalyzer::getMetrics() const
{
    std::lock_guard<std::mutex> lock(metricsMutex);
    return metrics;
}

int QualityAnalyzer::getCensoredWordCount() const
{
    std::lock_guard<std::mutex> lock(metricsMutex);
    return metrics.totalWordsCensored;
}

int QualityAnalyzer::getSkippedWordCount() const
{
    std::lock_guard<std::mutex> lock(metricsMutex);
    return metrics.totalWordsSkipped;
}

double QualityAnalyzer::getAverageRTF() const
{
    std::lock_guard<std::mutex> lock(metricsMutex);
    return metrics.averageRTF;
}

double QualityAnalyzer::getCurrentQualityScore() const
{
    std::lock_guard<std::mutex> lock(metricsMutex);
    return calculateQualityScore();
}

std::vector<CensorshipEvent> QualityAnalyzer::getRecentEvents(int maxCount) const
{
    std::lock_guard<std::mutex> lock(metricsMutex);
    
    int startIdx = std::max(0, (int)censorshipHistory.size() - maxCount);
    return std::vector<CensorshipEvent>(
        censorshipHistory.begin() + startIdx,
        censorshipHistory.end()
    );
}

double QualityAnalyzer::calculateQualityScore() const
{
    // Quality score from 0-100 based on multiple factors
    double score = 100.0;
    
    // Penalty for skipped words (buffer underruns)
    if (metrics.totalWordsDetected > 0)
    {
        double skipRate = (double)metrics.totalWordsSkipped / metrics.totalWordsDetected;
        score -= skipRate * 30.0;  // Up to -30 points for 100% skip rate
    }
    
    // Penalty for poor RTF performance (> 1.0 = processing too slow)
    if (metrics.averageRTF > 1.0)
    {
        double rtfPenalty = (metrics.averageRTF - 1.0) * 20.0;
        score -= std::min(rtfPenalty, 20.0);  // Up to -20 points
    }
    
    // Penalty for buffer underruns
    if (metrics.bufferUnderrunCount > 0)
    {
        score -= std::min(metrics.bufferUnderrunCount * 5.0, 20.0);  // Up to -20 points
    }
    
    // Penalty for clipping
    if (metrics.clippingEvents > 0)
    {
        score -= std::min(metrics.clippingEvents * 2.0, 15.0);  // Up to -15 points
    }
    
    // Bonus for multi-word detection (shows sophistication)
    if (metrics.totalWordsDetected > 0)
    {
        double multiWordRate = (double)metrics.multiWordDetections / metrics.totalWordsDetected;
        score += multiWordRate * 5.0;  // Up to +5 points bonus
    }
    
    return std::max(0.0, std::min(100.0, score));
}

std::string QualityAnalyzer::generateReport() const
{
    std::lock_guard<std::mutex> lock(metricsMutex);
    
    std::ostringstream report;
    report << std::fixed << std::setprecision(2);
    
    report << "========================================\n";
    report << "  EXPLICITLY QUALITY ANALYSIS REPORT\n";
    report << "========================================\n\n";
    
    report << "SESSION OVERVIEW:\n";
    report << "  Duration: " << metrics.sessionDuration << " seconds\n";
    report << "  Quality Score: " << calculateQualityScore() << "/100\n\n";
    
    report << "CENSORSHIP STATISTICS:\n";
    report << "  Total Words Detected: " << metrics.totalWordsDetected << "\n";
    report << "  Words Censored: " << metrics.totalWordsCensored << "\n";
    report << "  Words Skipped (underrun): " << metrics.totalWordsSkipped << "\n";
    report << "  Multi-word Detections: " << metrics.multiWordDetections << "\n";
    
    if (metrics.totalWordsDetected > 0)
    {
        double censorRate = (double)metrics.totalWordsCensored / metrics.totalWordsDetected * 100.0;
        report << "  Censor Success Rate: " << censorRate << "%\n";
    }
    report << "\n";
    
    report << "PERFORMANCE METRICS:\n";
    report << "  Average RTF: " << metrics.averageRTF << "x\n";
    report << "  Min RTF: " << metrics.minRTF << "x\n";
    report << "  Max RTF: " << metrics.maxRTF << "x\n";
    report << "  Buffer Underruns: " << metrics.bufferUnderrunCount << "\n\n";
    
    report << "BUFFER HEALTH:\n";
    report << "  Average Buffer: " << metrics.averageBufferSize << "s\n";
    report << "  Min Buffer: " << metrics.minBufferSize << "s\n";
    report << "  Max Buffer: " << metrics.maxBufferSize << "s\n\n";
    
    report << "AUDIO QUALITY:\n";
    report << "  Peak Level: " << (metrics.peakLevel * 100.0) << "%\n";
    report << "  Clipping Events: " << metrics.clippingEvents << "\n\n";
    
    report << "RECENT EVENTS:\n";
    int recentCount = std::min(10, (int)censorshipHistory.size());
    for (int i = censorshipHistory.size() - recentCount; i < censorshipHistory.size(); i++)
    {
        const auto& event = censorshipHistory[i];
        report << "  [" << event.timestamp << "s] \"" << event.word << "\" - ";
        report << (event.wasCensored ? event.mode : "SKIPPED") << "\n";
    }
    
    report << "\n========================================\n";
    
    return report.str();
}

bool QualityAnalyzer::exportToFile(const std::string& filename) const
{
    std::lock_guard<std::mutex> lock(metricsMutex);
    
    std::ofstream file(filename);
    if (!file.is_open())
    {
        std::cout << "[Phase8] ERROR: Could not open file " << filename << std::endl;
        return false;
    }
    
    file << generateReport();
    file.close();
    
    std::cout << "[Phase8] Report exported to " << filename << std::endl;
    return true;
}
