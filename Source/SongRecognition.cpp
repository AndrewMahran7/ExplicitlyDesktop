/*
  ==============================================================================

    SongRecognition.cpp
    Created: 19 Dec 2024
    Author: Explicitly Audio Systems

    Implementation of song identification using Chromaprint and lyrics fetching.

  ==============================================================================
*/

#include "SongRecognition.h"
#include "LyricsAlignment.h"
#include <juce_core/juce_core.h>
#include <iostream>

SongRecognition::SongRecognition()
{
}

bool SongRecognition::initialize(const std::string& fpcalcPath, const std::string& apiKey)
{
    fpcalcPath_ = fpcalcPath;
    acoustidApiKey_ = apiKey;
    
    // Validate fpcalc exists
    juce::String fpcalcPathJuce(fpcalcPath_);
    juce::File fpcalc(fpcalcPathJuce);
    if (!fpcalc.existsAsFile())
    {
        std::cout << "[SongRecognition] ERROR: fpcalc.exe not found at: " << fpcalcPath_ << std::endl;
        enabled_ = false;
        return false;
    }
    
    if (acoustidApiKey_.empty())
    {
        std::cout << "[SongRecognition] ERROR: Empty AcoustID API key" << std::endl;
        enabled_ = false;
        return false;
    }
    
    std::cout << "[SongRecognition] Initialized with Chromaprint + AcoustID" << std::endl;
    enabled_ = true;
    return true;
}

SongRecognition::SongInfo SongRecognition::identifySong(const float* audioBuffer, 
                                                        int numSamples, 
                                                        double sampleRate)
{
    SongInfo result;
    result.identified = false;
    result.confidence = 0.0f;
    
    if (!enabled_)
    {
        std::cout << "[SongRecognition] Song recognition disabled" << std::endl;
        return result;
    }
    
    std::cout << "[SongRecognition] Identifying song from " << numSamples << " samples @ " 
              << sampleRate << " Hz..." << std::endl;
    
    // Create Chromaprint fingerprint
    std::string fingerprint = createFingerprint(audioBuffer, numSamples, sampleRate);
    
    if (fingerprint.empty())
    {
        std::cout << "[SongRecognition] ERROR: Failed to create fingerprint" << std::endl;
        return result;
    }
    
    int duration = static_cast<int>(numSamples / sampleRate);
    
    // Query AcoustID
    std::string response = queryAcoustID(fingerprint, duration);
    
    if (response.empty())
    {
        std::cout << "[SongRecognition] ERROR: No response from AcoustID" << std::endl;
        return result;
    }
    
    // Parse response
    result = parseAcoustIDResponse(response);
    
    if (result.identified)
    {
        std::cout << "[SongRecognition] ✓ Identified: \"" << result.title << "\" by " 
                  << result.artist << " (confidence: " << (int)(result.confidence * 100) << "%)" << std::endl;
    }
    else
    {
        std::cout << "[SongRecognition] ✗ Song not identified" << std::endl;
    }
    
    return result;
}

std::string SongRecognition::fetchLyrics(const std::string& artist, const std::string& title)
{
    std::cout << "[SongRecognition] Fetching lyrics for: \"" << title << "\" by " << artist << std::endl;
    
    // Use LyricsAlignment::fetchLyrics which tries Genius -> lyrics.ovh
    ::SongInfo lyricsInfo = LyricsAlignment::fetchLyrics(artist, title);
    
    if (!lyricsInfo.lyrics.empty())
    {
        std::cout << "[SongRecognition] ✓ Lyrics found: " << lyricsInfo.lyrics.length() << " chars" << std::endl;
        return lyricsInfo.lyrics;
    }
    
    std::cout << "[SongRecognition] ✗ Lyrics not found" << std::endl;
    return "";
}

std::string SongRecognition::createFingerprint(const float* audioBuffer, int numSamples, double sampleRate)
{
    // Write PCM data to temporary raw file
    juce::File tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("explicitly_fingerprint.raw");
    
    std::cout << "[SongRecognition] Writing temp audio: " << tempFile.getFullPathName().toStdString() << std::endl;
    
    // Convert float to int16 and write raw PCM
    std::vector<int16_t> int16Samples(numSamples);
    for (int i = 0; i < numSamples; i++)
    {
        float sample = std::max(-1.0f, std::min(1.0f, audioBuffer[i]));
        int16Samples[i] = static_cast<int16_t>(sample * 32767.0f);
    }
    
    tempFile.replaceWithData(int16Samples.data(), numSamples * sizeof(int16_t));
    
    // Run fpcalc with raw PCM input
    juce::String fpcalcPathStr(fpcalcPath_);
    juce::String command = juce::String("\"") + fpcalcPathStr + "\" -raw -rate " + 
                          juce::String((int)sampleRate) + " -channels 1 -format s16le -length 120 \"" + 
                          tempFile.getFullPathName() + "\"";
    
    std::cout << "[SongRecognition] Running: " << command.toStdString() << std::endl;
    
    juce::ChildProcess process;
    if (!process.start(command))
    {
        std::cout << "[SongRecognition] ERROR: Failed to start fpcalc" << std::endl;
        tempFile.deleteFile();
        return "";
    }
    
    // Read output
    juce::String output = process.readAllProcessOutput();
    process.waitForProcessToFinish(10000); // 10 second timeout
    
    tempFile.deleteFile();
    
    // Parse fingerprint from output
    // Format: "FINGERPRINT=base64string"
    int fingerprintStart = output.indexOf("FINGERPRINT=");
    if (fingerprintStart < 0)
    {
        std::cout << "[SongRecognition] ERROR: No fingerprint in output" << std::endl;
        std::cout << "[SongRecognition] fpcalc output: " << output.toStdString() << std::endl;
        return "";
    }
    
    juce::String fingerprint = output.substring(fingerprintStart + 12).trim();
    std::cout << "[SongRecognition] Generated fingerprint: " << fingerprint.substring(0, 50).toStdString() << "..." << std::endl;
    
    return fingerprint.toStdString();
}

std::string SongRecognition::queryAcoustID(const std::string& fingerprint, int duration)
{
    // Build AcoustID API URL
    juce::String apiKeyStr(acoustidApiKey_);
    juce::String fingerprintStr(fingerprint);
    
    juce::String url = juce::String("https://api.acoustid.org/v2/lookup?client=") + apiKeyStr +
                      "&meta=recordings+releasegroups+compress&duration=" + juce::String(duration) +
                      "&fingerprint=" + juce::URL::addEscapeChars(fingerprintStr, false);
    
    std::cout << "[SongRecognition] Querying AcoustID..." << std::endl;
    
    juce::URL apiUrl(url);
    std::unique_ptr<juce::InputStream> stream(apiUrl.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(10000)
            .withNumRedirectsToFollow(3)));
    
    if (stream == nullptr)
    {
        std::cout << "[SongRecognition] Failed to connect to AcoustID" << std::endl;
        return "";
    }
    
    juce::String response = stream->readEntireStreamAsString();
    return response.toStdString();
}

SongRecognition::SongInfo SongRecognition::parseAcoustIDResponse(const std::string& response)
{
    SongInfo result;
    result.identified = false;
    
    auto json = juce::JSON::parse(juce::String(response));
    if (!json.isObject())
    {
        std::cout << "[SongRecognition] Invalid JSON response" << std::endl;
        return result;
    }
    
    auto root = json.getDynamicObject();
    
    // Check status
    if (!root->hasProperty("status") || root->getProperty("status").toString() != "ok")
    {
        std::cout << "[SongRecognition] AcoustID error: " << root->getProperty("error").toString().toStdString() << std::endl;
        return result;
    }
    
    // Get results array
    if (!root->hasProperty("results"))
    {
        std::cout << "[SongRecognition] No results in response" << std::endl;
        return result;
    }
    
    auto resultsArray = root->getProperty("results").getArray();
    if (resultsArray == nullptr || resultsArray->size() == 0)
    {
        std::cout << "[SongRecognition] Empty results array" << std::endl;
        return result;
    }
    
    // Get first (best) result
    auto firstResult = (*resultsArray)[0].getDynamicObject();
    result.confidence = (float)firstResult->getProperty("score");
    
    // Get recordings
    if (!firstResult->hasProperty("recordings"))
    {
        return result;
    }
    
    auto recordings = firstResult->getProperty("recordings").getArray();
    if (recordings == nullptr || recordings->size() == 0)
    {
        return result;
    }
    
    auto recording = (*recordings)[0].getDynamicObject();
    
    // Extract metadata
    result.title = recording->getProperty("title").toString().toStdString();
    
    if (recording->hasProperty("artists"))
    {
        auto artists = recording->getProperty("artists").getArray();
        if (artists != nullptr && artists->size() > 0)
        {
            auto artist = (*artists)[0].getDynamicObject();
            result.artist = artist->getProperty("name").toString().toStdString();
        }
    }
    
    if (recording->hasProperty("releasegroups"))
    {
        auto releasegroups = recording->getProperty("releasegroups").getArray();
        if (releasegroups != nullptr && releasegroups->size() > 0)
        {
            auto releasegroup = (*releasegroups)[0].getDynamicObject();
            result.album = releasegroup->getProperty("title").toString().toStdString();
        }
    }
    
    result.identified = !result.title.empty() && !result.artist.empty();
    
    return result;
}

std::string SongRecognition::fetchFromLyricsOVH(const std::string& artist, const std::string& title)
{
    // Simple HTTP GET to LyricsOVH (no auth required)
    // Endpoint: https://api.lyrics.ovh/v1/{artist}/{title}
    
    try
    {
        juce::String artistStr(artist);
        juce::String titleStr(title);
        juce::String url = juce::String("https://api.lyrics.ovh/v1/") 
                         + juce::URL::addEscapeChars(artistStr, false) + "/" 
                         + juce::URL::addEscapeChars(titleStr, false);
        
        std::cout << "[SongRecognition] Querying: " << url.toStdString() << std::endl;
        
        juce::URL apiUrl(url);
        std::unique_ptr<juce::InputStream> stream(apiUrl.createInputStream(
            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs(5000)
                .withNumRedirectsToFollow(3)));
        
        if (stream == nullptr)
        {
            std::cout << "[SongRecognition] Failed to connect to LyricsOVH" << std::endl;
            return "";
        }
        
        juce::String response = stream->readEntireStreamAsString();
        
        // Parse JSON response: { "lyrics": "..." }
        auto json = juce::JSON::parse(response);
        if (json.isObject())
        {
            auto obj = json.getDynamicObject();
            if (obj->hasProperty("lyrics"))
            {
                return obj->getProperty("lyrics").toString().toStdString();
            }
        }
        
        std::cout << "[SongRecognition] No lyrics field in response" << std::endl;
        return "";
    }
    catch (const std::exception& e)
    {
        std::cout << "[SongRecognition] Exception: " << e.what() << std::endl;
        return "";
    }
}
