/*
  ==============================================================================

    ASRThread.cpp
    Created: 9 Dec 2024
    Author: Explicitly Audio Systems

    Vosk ASR thread implementation.

  ==============================================================================
*/

#include "ASRThread.h"
#include "CircularBuffer.h"
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>

ASRThread::ASRThread(int sr)
    : sampleRate(sr)
{
}

ASRThread::~ASRThread()
{
    stop();
    
    if (voskRecognizer)
    {
        vosk_recognizer_free(voskRecognizer);
        voskRecognizer = nullptr;
    }
    
    if (voskModel)
    {
        vosk_model_free(voskModel);
        voskModel = nullptr;
    }
}

bool ASRThread::start(LockFreeQueue<AudioChunk, 64>* audioQ,
                     LockFreeQueue<CensorEvent, 256>* censorQ,
                     CircularAudioBuffer* circBuffer)
{
    if (running.load())
    {
        juce::Logger::writeToLog("[ASRThread] Already running");
        return false;
    }
    
    audioQueue = audioQ;
    censorQueue = censorQ;
    circularBuffer = circBuffer;
    
    // Load Vosk model (use executable directory, not working directory)
    juce::File exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    juce::File modelPath = exeDir.getChildFile("Models").getChildFile("vosk-model-small-en-us");
    
    if (!modelPath.exists())
    {
        lastError = "Vosk model not found at: " + modelPath.getFullPathName();
        juce::Logger::writeToLog("[ASRThread] ERROR: " + lastError);
        return false;
    }
    
    voskModel = vosk_model_new(modelPath.getFullPathName().toRawUTF8());
    
    if (!voskModel)
    {
        lastError = "Failed to load Vosk model from: " + modelPath.getFullPathName();
        juce::Logger::writeToLog("[ASRThread] ERROR: " + lastError);
        return false;
    }
    
    juce::Logger::writeToLog("[ASRThread] Vosk model loaded successfully");
    
    // Create recognizer
    voskRecognizer = vosk_recognizer_new(voskModel, static_cast<float>(sampleRate));
    
    if (!voskRecognizer)
    {
        lastError = "Failed to create Vosk recognizer";
        juce::Logger::writeToLog("[ASRThread] ERROR: " + lastError);
        vosk_model_free(voskModel);
        voskModel = nullptr;
        return false;
    }
    
    // Set recognizer to return partial results
    vosk_recognizer_set_max_alternatives(voskRecognizer, 0);
    vosk_recognizer_set_words(voskRecognizer, 1);  // Enable word-level timestamps
    
    // Load profanity lexicon (use executable directory)
    juce::File lexiconPath = exeDir.getChildFile("lexicons").getChildFile("profanity_en.txt");
    
    if (!profanityFilter.loadLexicon(lexiconPath))
    {
        lastError = "Failed to load profanity lexicon from: " + lexiconPath.getFullPathName();
        juce::Logger::writeToLog("[ASRThread] ERROR: " + lastError);
        vosk_recognizer_free(voskRecognizer);
        vosk_model_free(voskModel);
        voskRecognizer = nullptr;
        voskModel = nullptr;
        return false;
    }
    
    juce::Logger::writeToLog("[ASRThread] Profanity lexicon loaded successfully");
    
    // Start processing thread
    juce::Logger::writeToLog("[ASRThread] Starting processing thread...");
    
    running.store(true);
    processingThread = std::make_unique<std::thread>([this]() { run(); });
    
    juce::Logger::writeToLog("[ASRThread] Started successfully");
    
    if (debugCallback)
        debugCallback("[ASR] Thread started successfully");
    
    return true;
}

void ASRThread::stop()
{
    if (!running.load())
        return;
    
    running.store(false);
    
    if (processingThread && processingThread->joinable())
    {
        processingThread->join();
    }
    
    juce::Logger::writeToLog("[ASRThread] Stopped");
}

void ASRThread::run()
{
    juce::Logger::writeToLog("[ASRThread] Processing loop started");
    
    int chunksProcessed = 0;
    auto lastDebugTime = std::chrono::steady_clock::now();
    
    try
    {
        while (running.load())
        {
            // Get audio chunk from queue (non-blocking)
            auto chunkOpt = audioQueue->pop();
            
            if (chunkOpt.has_value())
            {
                processAudioChunk(chunkOpt.value());
                chunksProcessed++;
                
                // Log every 100 chunks (~2 seconds)
                auto now = std::chrono::steady_clock::now();
                if (chunksProcessed % 100 == 0 || 
                    std::chrono::duration_cast<std::chrono::seconds>(now - lastDebugTime).count() >= 5)
                {
                    juce::String msg = "[ASR] Processed " + juce::String(chunksProcessed) + " audio chunks";
                    juce::Logger::writeToLog(msg);
                    if (debugCallback)
                        debugCallback(msg);
                    lastDebugTime = now;
                }
            }
            else
            {
                // No audio available, sleep briefly
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("[ASRThread] FATAL ERROR in processing thread: " + juce::String(e.what()));
        if (debugCallback)
            debugCallback("[ASR] FATAL ERROR: " + juce::String(e.what()));
    }
    
    juce::Logger::writeToLog("[ASRThread] Processing thread stopped");
}

void ASRThread::processAudioChunk(const AudioChunk& chunk)
{
    static std::atomic<int> chunkCount{0};
    int currentChunk = chunkCount.fetch_add(1);
    
    if (currentChunk == 0)
        juce::Logger::writeToLog("[ASRThread] Processing first audio chunk");
    
    if (!voskRecognizer || !circularBuffer || chunk.num_samples <= 0 || chunk.num_channels <= 0)
        return;
    
    // Read audio data from CircularBuffer using metadata
    juce::AudioBuffer<float> audioData;
    if (!circularBuffer->readSamplesAt(audioData, chunk.buffer_position, chunk.num_samples))
    {
        // Failed to read - buffer may have wrapped around
        return;
    }
    
    // Downmix to mono if needed (Vosk expects mono)
    juce::AudioBuffer<float> monoBuffer;
    if (chunk.num_channels > 1)
    {
        monoBuffer.setSize(1, chunk.num_samples);
        monoBuffer.clear();
        for (int ch = 0; ch < chunk.num_channels; ++ch)
        {
            monoBuffer.addFrom(0, 0, audioData, ch, 0, chunk.num_samples, 1.0f / chunk.num_channels);
        }
    }
    else
    {
        monoBuffer = audioData;
    }
    
    // Convert float samples to 16-bit PCM for Vosk
    std::vector<int16_t> pcmData(chunk.num_samples);
    const float* monoSamples = monoBuffer.getReadPointer(0);
    for (int i = 0; i < chunk.num_samples; ++i)
    {
        float sample = monoSamples[i];
        sample = std::max(-1.0f, std::min(1.0f, sample));  // Clamp
        pcmData[i] = static_cast<int16_t>(sample * 32767.0f);
    }
    
    // Validate before calling Vosk
    if (!voskRecognizer || pcmData.empty())
    {
        juce::Logger::writeToLog("[ASRThread] ERROR: Invalid recognizer or empty PCM data");
        return;
    }
    
    if (currentChunk == 0)
        juce::Logger::writeToLog("[ASRThread] About to call vosk_recognizer_accept_waveform");
    
    int result = 0;
    try
    {
        // Feed audio to Vosk recognizer - NOTE: Third parameter is size in BYTES
        result = vosk_recognizer_accept_waveform(
            voskRecognizer,
            reinterpret_cast<const char*>(pcmData.data()),
            static_cast<int>(pcmData.size() * sizeof(int16_t))
        );
        
        if (currentChunk == 0)
            juce::Logger::writeToLog("[ASRThread] vosk_recognizer_accept_waveform succeeded, result=" + juce::String(result));
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("[ASRThread] EXCEPTION in vosk_recognizer_accept_waveform: " + juce::String(e.what()));
        return;
    }
    catch (...)
    {
        juce::Logger::writeToLog("[ASRThread] UNKNOWN EXCEPTION in vosk_recognizer_accept_waveform");
        return;
    }
    
    // Get result (partial or final)
    const char* json = nullptr;
    
    try
    {
        if (result)
        {
            // Final result (end of utterance detected)
            if (currentChunk == 0)
                juce::Logger::writeToLog("[ASRThread] Getting final result");
            
            json = vosk_recognizer_result(voskRecognizer);
            
            if (currentChunk == 0)
                juce::Logger::writeToLog("[ASRThread] Final result retrieved");
            
            if (debugCallback && json && strlen(json) > 10)
                debugCallback("[ASR] Final result received");
        }
        else
        {
            // Partial result (continuous streaming)
            if (currentChunk == 0)
                juce::Logger::writeToLog("[ASRThread] Getting partial result");
            
            json = vosk_recognizer_partial_result(voskRecognizer);
            
            if (currentChunk == 0)
                juce::Logger::writeToLog("[ASRThread] Partial result retrieved");
        }
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("[ASRThread] EXCEPTION getting Vosk result: " + juce::String(e.what()));
        return;
    }
    catch (...)
    {
        juce::Logger::writeToLog("[ASRThread] UNKNOWN EXCEPTION getting Vosk result");
        return;
    }
    
    if (json && strlen(json) > 0)
    {
        parseVoskResult(json, chunk.buffer_position);
    }
}

void ASRThread::parseVoskResult(const char* json, int64_t bufferPosition)
{
    // Parse JSON result from Vosk
    juce::var parsedResult = juce::JSON::parse(json);
    
    if (!parsedResult.isObject())
        return;
    
    auto* resultObj = parsedResult.getDynamicObject();
    if (!resultObj)
        return;
    
    // Check for "result" array (contains word-level timestamps)
    juce::var resultArray = resultObj->getProperty("result");
    if (!resultArray.isArray())
    {
        // Try "partial" for partial results
        resultArray = resultObj->getProperty("partial");
        if (!resultArray.isArray())
            return;
    }
    
    // Build list of words with timestamps
    std::vector<ProfanityFilter::Word> words;
    
    if (resultArray.isArray())
    {
        auto* arr = resultArray.getArray();
        for (int i = 0; i < arr->size(); ++i)
        {
            auto wordObj = arr->getUnchecked(i).getDynamicObject();
            if (wordObj)
            {
                ProfanityFilter::Word word;
                word.text = wordObj->getProperty("word").toString().toLowerCase().toStdString();
                word.start_time = static_cast<double>(wordObj->getProperty("start"));
                word.end_time = static_cast<double>(wordObj->getProperty("end"));
                
                words.push_back(word);
            }
        }
    }
    
    if (words.empty())
        return;
    
    // Detect profanity
    auto profanitySpans = profanityFilter.detectProfanity(words);
    
    // Send transcript to UI (before profanity detection)
    if (debugCallback && !words.empty())
    {
        juce::String transcript;
        for (const auto& w : words)
            transcript += juce::String(w.text) + " ";
        
        juce::String msg = "[ASR] " + transcript.trim();
        debugCallback(msg);
    }
    
    // Send censorship events to audio thread
    for (const auto& span : profanitySpans)
    {
        CensorEvent event;
        event.start_sample = bufferPosition + static_cast<int64_t>(span.start_time * sampleRate);
        event.end_sample = bufferPosition + static_cast<int64_t>(span.end_time * sampleRate);
        event.mode = CensorEvent::Mode::Reverse;  // TODO: Make configurable
        
        // Copy word for debugging
        std::strncpy(event.word, span.text.c_str(), sizeof(event.word) - 1);
        event.word[sizeof(event.word) - 1] = '\0';
        event.confidence = 1.0;  // Vosk doesn't provide per-word confidence in this mode
        
        if (censorQueue->push(event))
        {
            juce::String debugMsg = "[PROFANITY DETECTED] \"" + juce::String(span.text) + 
                                   "\" | Time: " + juce::String(span.start_time, 2) + "s - " + 
                                   juce::String(span.end_time, 2) + "s" +
                                   " | Samples: " + juce::String(event.start_sample) + " - " + 
                                   juce::String(event.end_sample);
            
            juce::Logger::writeToLog("[ASRThread] " + debugMsg);
            
            if (debugCallback)
                debugCallback(debugMsg);
        }
        else
        {
            juce::Logger::writeToLog("[ASRThread] WARNING: Censor queue full, event dropped");
            
            if (debugCallback)
                debugCallback("[WARNING] Censor queue full, event dropped");
        }
    }
}
