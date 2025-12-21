/*
  ==============================================================================

    TimestampRefiner.cpp
    Created: 10 Dec 2024
    Author: Explicitly Audio Systems

    Implementation of timestamp refinement using audio energy analysis.

  ==============================================================================
*/

#include "TimestampRefiner.h"
#include <iostream>
#include <numeric>
#include <iomanip>

float TimestampRefiner::calculateEnergy(const std::vector<float>& audio, 
                                       int start, int length)
{
    if (start < 0 || start + length > (int)audio.size())
        return 0.0f;
    
    float sum = 0.0f;
    for (int i = start; i < start + length; ++i)
    {
        float sample = audio[i];
        sum += sample * sample;
    }
    
    return std::sqrt(sum / length);
}

float TimestampRefiner::calculateZeroCrossing(const std::vector<float>& audio,
                                             int start, int length)
{
    if (start < 0 || start + length > (int)audio.size())
        return 0.0f;
    
    int crossings = 0;
    for (int i = start + 1; i < start + length; ++i)
    {
        if ((audio[i] >= 0.0f && audio[i - 1] < 0.0f) ||
            (audio[i] < 0.0f && audio[i - 1] >= 0.0f))
        {
            crossings++;
        }
    }
    
    return (float)crossings / length;
}

double TimestampRefiner::findBestBoundary(const std::vector<float>& audio,
                                         int centerSample,
                                         int searchRadius,
                                         int sampleRate,
                                         bool findStart)
{
    // Search direction: backwards for start, forwards for end
    int step = findStart ? -1 : 1;
    int searchStart = std::max(0, centerSample - (findStart ? searchRadius : 0));
    int searchEnd = std::min((int)audio.size(), centerSample + (findStart ? 0 : searchRadius));
    
    // Find the point with steepest energy change
    float bestScore = -1.0f;
    int bestSample = centerSample;
    
    for (int i = searchStart; i < searchEnd; i += WINDOW_SIZE / 4)  // Step by 2.5ms
    {
        if (i < WINDOW_SIZE || i + WINDOW_SIZE >= (int)audio.size())
            continue;
        
        // Calculate energy gradient
        float energyBefore = calculateEnergy(audio, i - WINDOW_SIZE, WINDOW_SIZE);
        float energyAfter = calculateEnergy(audio, i, WINDOW_SIZE);
        
        float gradient = std::abs(energyAfter - energyBefore);
        
        // For start: look for energy rise; for end: look for energy drop
        float score = findStart ? (energyAfter - energyBefore) : (energyBefore - energyAfter);
        
        if (score > bestScore && gradient > ENERGY_THRESHOLD)
        {
            bestScore = score;
            bestSample = i;
        }
    }
    
    return (double)bestSample / sampleRate;
}

std::pair<double, double> TimestampRefiner::searchForSpeech(
    const std::vector<float>& audio,
    double whisperStart,
    double whisperEnd,
    int sampleRate)
{
    // Convert whisper timestamps to samples
    int whisperStartSample = (int)(whisperStart * sampleRate);
    int whisperEndSample = (int)(whisperEnd * sampleRate);
    
    // Clamp to buffer bounds
    whisperStartSample = std::max(0, std::min(whisperStartSample, (int)audio.size() - 1));
    whisperEndSample = std::max(whisperStartSample, std::min(whisperEndSample, (int)audio.size()));
    
    // Search for actual speech energy around Whisper's guess
    // Strategy: Find energy peaks within search radius
    
    int searchRadius = SEARCH_RADIUS;
    int searchStart = std::max(0, whisperStartSample - searchRadius);
    int searchEnd = std::min((int)audio.size(), whisperEndSample + searchRadius);
    
    // Find regions with significant energy
    std::vector<std::pair<int, int>> energyRegions;
    bool inSpeech = false;
    int regionStart = 0;
    
    for (int i = searchStart; i < searchEnd; i += WINDOW_SIZE)
    {
        float energy = calculateEnergy(audio, i, WINDOW_SIZE);
        float zc = calculateZeroCrossing(audio, i, WINDOW_SIZE);
        
        bool isSpeech = (energy > ENERGY_THRESHOLD && zc > ZC_THRESHOLD);
        
        if (isSpeech && !inSpeech)
        {
            regionStart = i;
            inSpeech = true;
        }
        else if (!isSpeech && inSpeech)
        {
            energyRegions.emplace_back(regionStart, i);
            inSpeech = false;
        }
    }
    
    if (inSpeech)
        energyRegions.emplace_back(regionStart, searchEnd);
    
    // If no speech regions found, return original Whisper timestamps
    if (energyRegions.empty())
    {
        return {whisperStart, whisperEnd};
    }
    
    // Find the energy region closest to Whisper's timestamps
    // For tiny model: prefer regions BEFORE Whisper's guess (it tends to timestamp late)
    int whisperCenter = (whisperStartSample + whisperEndSample) / 2;
    int closestDist = INT_MAX;
    std::pair<int, int> bestRegion = energyRegions[0];
    
    for (const auto& region : energyRegions)
    {
        int regionCenter = (region.first + region.second) / 2;
        int dist = std::abs(regionCenter - whisperCenter);
        
        // Bias towards earlier regions (compensate for tiny model's late timestamps)
        // If region is before Whisper center, reduce its distance by 20%
        if (regionCenter < whisperCenter)
        {
            dist = (int)(dist * 0.8);  // Prefer earlier regions
        }
        
        if (dist < closestDist)
        {
            closestDist = dist;
            bestRegion = region;
        }
    }
    
    // Refine boundaries of best region
    double refinedStart = findBestBoundary(audio, bestRegion.first, WINDOW_SIZE * 4, sampleRate, true);
    double refinedEnd = findBestBoundary(audio, bestRegion.second, WINDOW_SIZE * 4, sampleRate, false);
    
    // Sanity checks
    if (refinedEnd <= refinedStart)
        refinedEnd = refinedStart + MIN_WORD_DURATION;
    
    if (refinedEnd - refinedStart > MAX_WORD_DURATION)
        refinedEnd = refinedStart + MAX_WORD_DURATION;
    
    return {refinedStart, refinedEnd};
}

void TimestampRefiner::refineWordTimestamp(WordSegment& word, 
                                          const std::vector<float>& audio,
                                          int sampleRate)
{
    // Store original Whisper timestamps for debugging
    double originalStart = word.start;
    double originalEnd = word.end;
    
    // Calculate max energy in search region for debugging
    int searchStart = std::max(0, (int)(originalStart * sampleRate) - SEARCH_RADIUS);
    int searchEnd = std::min((int)audio.size(), (int)(originalEnd * sampleRate) + SEARCH_RADIUS);
    float maxEnergy = 0.0f;
    for (int i = searchStart; i < searchEnd; i += WINDOW_SIZE)
    {
        float e = calculateEnergy(audio, i, WINDOW_SIZE);
        maxEnergy = std::max(maxEnergy, e);
    }
    
    // Search for actual speech content
    auto [refinedStart, refinedEnd] = searchForSpeech(audio, originalStart, originalEnd, sampleRate);
    
    // Update word segment
    word.start = refinedStart;
    word.end = refinedEnd;
    
    // Debug output - show ALL attempts (not just changes)
    double delta = refinedStart - originalStart;
    if (std::abs(delta) > 0.01)  // Show changes > 10ms
    {
        std::cout << "[Refiner] \"" << word.word << "\": "
                  << std::fixed << std::setprecision(2)
                  << originalStart << "s-" << originalEnd << "s -> "
                  << refinedStart << "s-" << refinedEnd << "s "
                  << "(delta=" << delta << "s, maxE=" << maxEnergy << ")" << std::endl;
    }
    else if (originalStart < 0.15)  // Debug stuck-at-zero issue
    {
        std::cout << "[Refiner] \"" << word.word << "\": NO CHANGE (Whisper: "
                  << originalStart << "s-" << originalEnd << "s, maxE=" << maxEnergy << ")" << std::endl;
    }
}

std::vector<std::pair<double, double>> TimestampRefiner::findSpeechRegions(
    const std::vector<float>& audio,
    int sampleRate)
{
    std::vector<std::pair<double, double>> regions;
    
    bool inSpeech = false;
    int regionStart = 0;
    
    for (int i = 0; i < (int)audio.size(); i += WINDOW_SIZE)
    {
        float energy = calculateEnergy(audio, i, WINDOW_SIZE);
        float zc = calculateZeroCrossing(audio, i, WINDOW_SIZE);
        
        bool isSpeech = (energy > ENERGY_THRESHOLD && zc > ZC_THRESHOLD);
        
        if (isSpeech && !inSpeech)
        {
            regionStart = i;
            inSpeech = true;
        }
        else if (!isSpeech && inSpeech)
        {
            regions.emplace_back((double)regionStart / sampleRate, (double)i / sampleRate);
            inSpeech = false;
        }
    }
    
    if (inSpeech)
        regions.emplace_back((double)regionStart / sampleRate, (double)audio.size() / sampleRate);
    
    return regions;
}
