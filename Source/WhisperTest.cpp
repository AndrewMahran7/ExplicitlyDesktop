/*
  ==============================================================================

    WhisperTest.cpp
    Created: 10 Dec 2024
    Author: Explicitly Audio Systems

    Phase 3: Standalone Whisper.cpp test
    
    Tests Whisper inference in isolation without JUCE, threading, or live audio.
    Loads a WAV file, runs transcription, prints result.

  ==============================================================================
*/

#include <whisper.h>
#include <iostream>
#include <vector>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <chrono>

// Simple WAV header parser
struct WavHeader {
    char riff[4];           // "RIFF"
    uint32_t fileSize;
    char wave[4];           // "WAVE"
    char fmt[4];            // "fmt "
    uint32_t fmtSize;
    uint16_t audioFormat;   // 1 = PCM
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
};

bool loadWavFile(const char* filename, std::vector<float>& audioData, int& sampleRate, int& channels)
{
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "Failed to open WAV file: " << filename << std::endl;
        return false;
    }

    // Read RIFF header
    char riff[4];
    uint32_t fileSize;
    char wave[4];
    
    file.read(riff, 4);
    file.read(reinterpret_cast<char*>(&fileSize), sizeof(uint32_t));
    file.read(wave, 4);
    
    if (std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(wave, "WAVE", 4) != 0)
    {
        std::cerr << "Not a valid WAV file (missing RIFF/WAVE header)" << std::endl;
        return false;
    }
    
    // Initialize header with defaults
    WavHeader header;
    std::memset(&header, 0, sizeof(header));
    bool foundFmt = false;
    bool foundData = false;
    uint32_t dataSize = 0;
    
    // Read chunks until we find both fmt and data
    while (!file.eof() && file.good())
    {
        char chunkId[4];
        uint32_t chunkSize;
        
        if (!file.read(chunkId, 4))
            break;
        if (!file.read(reinterpret_cast<char*>(&chunkSize), sizeof(uint32_t)))
            break;
            
        if (std::memcmp(chunkId, "fmt ", 4) == 0)
        {
            // Read fmt chunk
            file.read(reinterpret_cast<char*>(&header.audioFormat), sizeof(uint16_t));
            file.read(reinterpret_cast<char*>(&header.numChannels), sizeof(uint16_t));
            file.read(reinterpret_cast<char*>(&header.sampleRate), sizeof(uint32_t));
            file.read(reinterpret_cast<char*>(&header.byteRate), sizeof(uint32_t));
            file.read(reinterpret_cast<char*>(&header.blockAlign), sizeof(uint16_t));
            file.read(reinterpret_cast<char*>(&header.bitsPerSample), sizeof(uint16_t));
            
            // Skip any extra fmt data
            uint32_t extraSize = chunkSize - 16;
            if (extraSize > 0)
                file.seekg(extraSize, std::ios::cur);
                
            foundFmt = true;
        }
        else if (std::memcmp(chunkId, "data", 4) == 0)
        {
            dataSize = chunkSize;
            foundData = true;
            break;  // Data chunk is the last one we need
        }
        else
        {
            // Skip unknown chunk
            file.seekg(chunkSize, std::ios::cur);
        }
    }
    
    if (!foundFmt || !foundData)
    {
        std::cerr << "Invalid WAV file (missing fmt or data chunk)" << std::endl;
        std::cerr << "  Found fmt: " << (foundFmt ? "yes" : "no") << std::endl;
        std::cerr << "  Found data: " << (foundData ? "yes" : "no") << std::endl;
        return false;
    }

    // Validate format (1 = PCM, 3 = IEEE float)
    if (header.audioFormat != 1 && header.audioFormat != 3)
    {
        std::cerr << "Unsupported WAV format: " << header.audioFormat << " (only PCM and float supported)" << std::endl;
        return false;
    }

    sampleRate = header.sampleRate;
    channels = header.numChannels;

    std::cout << "[WhisperTest] WAV file info:" << std::endl;
    std::cout << "  Sample rate: " << sampleRate << " Hz" << std::endl;
    std::cout << "  Channels: " << channels << std::endl;
    std::cout << "  Bits per sample: " << header.bitsPerSample << std::endl;
    std::cout << "  Data size: " << dataSize << " bytes" << std::endl;

    // Read audio data
    int numSamples = dataSize / (header.bitsPerSample / 8);
    audioData.resize(numSamples);

    if (header.audioFormat == 1)  // PCM
    {
        if (header.bitsPerSample == 16)
        {
            std::vector<int16_t> rawData(numSamples);
            file.read(reinterpret_cast<char*>(rawData.data()), dataSize);
            
            // Convert to float [-1.0, 1.0]
            for (int i = 0; i < numSamples; ++i)
                audioData[i] = rawData[i] / 32768.0f;
        }
        else if (header.bitsPerSample == 32)
        {
            std::vector<int32_t> rawData(numSamples);
            file.read(reinterpret_cast<char*>(rawData.data()), dataSize);
            
            // Convert to float [-1.0, 1.0]
            for (int i = 0; i < numSamples; ++i)
                audioData[i] = rawData[i] / 2147483648.0f;
        }
        else
        {
            std::cerr << "Unsupported PCM bit depth: " << header.bitsPerSample << std::endl;
            return false;
        }
    }
    else if (header.audioFormat == 3)  // IEEE Float
    {
        if (header.bitsPerSample == 32)
        {
            // Already in float format
            file.read(reinterpret_cast<char*>(audioData.data()), dataSize);
        }
        else if (header.bitsPerSample == 64)
        {
            std::vector<double> rawData(numSamples);
            file.read(reinterpret_cast<char*>(rawData.data()), dataSize);
            
            // Convert double to float
            for (int i = 0; i < numSamples; ++i)
                audioData[i] = static_cast<float>(rawData[i]);
        }
        else
        {
            std::cerr << "Unsupported float bit depth: " << header.bitsPerSample << std::endl;
            return false;
        }
    }

    std::cout << "  Total samples: " << numSamples << std::endl;
    std::cout << "  Duration: " << (numSamples / channels / (float)sampleRate) << " seconds" << std::endl;

    return true;
}

std::vector<float> resampleTo16kHz(const std::vector<float>& input, int inputRate, int channels)
{
    if (inputRate == 16000)
        return input;

    std::cout << "[WhisperTest] Resampling from " << inputRate << " Hz to 16000 Hz..." << std::endl;

    // Downmix to mono if stereo
    std::vector<float> mono;
    if (channels == 2)
    {
        mono.resize(input.size() / 2);
        for (size_t i = 0; i < mono.size(); ++i)
            mono[i] = (input[i * 2] + input[i * 2 + 1]) * 0.5f;
    }
    else
    {
        mono = input;
    }

    // Simple linear interpolation resample
    double ratio = (double)inputRate / 16000.0;
    size_t outputSize = (size_t)(mono.size() / ratio);
    std::vector<float> output(outputSize);

    for (size_t i = 0; i < outputSize; ++i)
    {
        double srcPos = i * ratio;
        size_t srcIndex = (size_t)srcPos;
        double frac = srcPos - srcIndex;

        if (srcIndex + 1 < mono.size())
            output[i] = mono[srcIndex] * (1.0f - frac) + mono[srcIndex + 1] * frac;
        else
            output[i] = mono[srcIndex];
    }

    std::cout << "  Resampled to " << output.size() << " samples (" 
              << (output.size() / 16000.0f) << " seconds)" << std::endl;

    return output;
}

int main(int argc, char* argv[])
{
    std::cout << "============================================" << std::endl;
    std::cout << "  Whisper.cpp Standalone Test - Phase 3" << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << std::endl;

    // Check command line arguments
    if (argc < 2)
    {
        std::cerr << "Usage: WhisperTest <audio.wav>" << std::endl;
        std::cerr << std::endl;
        std::cerr << "Example: WhisperTest test.wav" << std::endl;
        return 1;
    }

    const char* wavFile = argv[1];
    const char* modelPath = "Models/ggml-tiny.en.bin";

    // Load WAV file
    std::cout << "[WhisperTest] Loading WAV file: " << wavFile << std::endl;
    std::vector<float> audioData;
    int sampleRate, channels;
    
    if (!loadWavFile(wavFile, audioData, sampleRate, channels))
    {
        std::cerr << "[WhisperTest] Failed to load WAV file" << std::endl;
        return 1;
    }
    std::cout << std::endl;

    // Resample to 16kHz mono
    std::vector<float> audio16k = resampleTo16kHz(audioData, sampleRate, channels);
    std::cout << std::endl;

    // Load Whisper model
    std::cout << "[WhisperTest] Loading Whisper model: " << modelPath << std::endl;
    whisper_context_params cparams = whisper_context_default_params();
    whisper_context* ctx = whisper_init_from_file_with_params(modelPath, cparams);
    
    if (ctx == nullptr)
    {
        std::cerr << "[WhisperTest] ERROR: Failed to load Whisper model" << std::endl;
        return 1;
    }
    std::cout << "[WhisperTest] Model loaded successfully" << std::endl;
    std::cout << std::endl;

    // Configure Whisper parameters
    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.print_realtime = false;
    wparams.print_progress = false;
    wparams.print_timestamps = false;
    wparams.print_special = false;
    wparams.translate = false;
    wparams.language = "en";
    wparams.n_threads = 4;
    wparams.single_segment = false;

    // Run transcription
    std::cout << "[WhisperTest] Running transcription..." << std::endl;
    auto startTime = std::chrono::high_resolution_clock::now();
    
    int result = whisper_full(ctx, wparams, audio16k.data(), (int)audio16k.size());
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    if (result != 0)
    {
        std::cerr << "[WhisperTest] ERROR: Transcription failed with code " << result << std::endl;
        whisper_free(ctx);
        return 1;
    }

    std::cout << "[WhisperTest] Transcription completed in " << duration << " ms" << std::endl;
    std::cout << std::endl;

    // Extract and print results
    int numSegments = whisper_full_n_segments(ctx);
    std::cout << "============================================" << std::endl;
    std::cout << "  TRANSCRIPT (" << numSegments << " segments)" << std::endl;
    std::cout << "============================================" << std::endl;

    for (int i = 0; i < numSegments; ++i)
    {
        const char* text = whisper_full_get_segment_text(ctx, i);
        int64_t t0 = whisper_full_get_segment_t0(ctx, i);
        int64_t t1 = whisper_full_get_segment_t1(ctx, i);
        
        float startSec = t0 / 100.0f;
        float endSec = t1 / 100.0f;
        
        std::cout << "[" << startSec << "s - " << endSec << "s] " << text << std::endl;
    }

    std::cout << "============================================" << std::endl;
    std::cout << std::endl;

    // Cleanup
    whisper_free(ctx);

    std::cout << "[WhisperTest] Test completed successfully!" << std::endl;
    return 0;
}
