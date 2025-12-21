/*
  ==============================================================================

    WhisperThread.cpp
    Created: 9 Dec 2024
    Author: Explicitly Audio Systems

    Whisper.cpp ASR thread implementation.

  ==============================================================================
*/

#include "WhisperThread.h"
#include "CircularBuffer.h"
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <iostream>

WhisperThread::WhisperThread(int sr)
    : sampleRate(sr)
{
}

WhisperThread::~WhisperThread()
{
    stop();
    
    if (whisperCtx)
    {
        whisper_free(whisperCtx);
        whisperCtx = nullptr;
    }
}

bool WhisperThread::start(LockFreeQueue<AudioChunk, 64>* audioQ,
                          LockFreeQueue<CensorEvent, 256>* censorQ,
                          CircularAudioBuffer* circBuffer)
{
    if (running.load())
    {
        juce::Logger::writeToLog("[WhisperThread] Already running");
        return false;
    }
    
    audioQueue = audioQ;
    censorQueue = censorQ;
    circularBuffer = circBuffer;
    
    // Load Whisper model
    juce::File exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    juce::File modelPath = exeDir.getChildFile("Models").getChildFile("ggml-tiny.en.bin");
    
    if (!modelPath.exists())
    {
        lastError = "Whisper model not found at: " + modelPath.getFullPathName();
        juce::Logger::writeToLog("[WhisperThread] ERROR: " + lastError);
        return false;
    }
    
    juce::Logger::writeToLog("[WhisperThread] Loading Whisper model from: " + modelPath.getFullPathName());
    std::cout << "[WhisperThread] Loading Whisper model..." << std::endl;
    
    whisperCtx = whisper_init_from_file(modelPath.getFullPathName().toRawUTF8());
    
    if (!whisperCtx)
    {
        lastError = "Failed to load Whisper model from: " + modelPath.getFullPathName();
        juce::Logger::writeToLog("[WhisperThread] ERROR: " + lastError);
        return false;
    }
    
    juce::Logger::writeToLog("[WhisperThread] Whisper model loaded successfully");
    std::cout << "[WhisperThread] Whisper model loaded successfully" << std::endl;
    
    // Initialize Whisper parameters for streaming
    whisperParams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    whisperParams.print_realtime = false;
    whisperParams.print_progress = false;
    whisperParams.print_timestamps = true;
    whisperParams.print_special = false;
    whisperParams.translate = false;
    whisperParams.language = "en";
    whisperParams.n_threads = 4;
    whisperParams.offset_ms = 0;
    whisperParams.no_context = true;
    whisperParams.single_segment = false;
    
    // Load profanity lexicon
    juce::File lexiconPath = exeDir.getChildFile("lexicons").getChildFile("profanity_en.txt");
    
    if (!profanityFilter.loadLexicon(lexiconPath))
    {
        lastError = "Failed to load profanity lexicon from: " + lexiconPath.getFullPathName();
        juce::Logger::writeToLog("[WhisperThread] ERROR: " + lastError);
        whisper_free(whisperCtx);
        whisperCtx = nullptr;
        return false;
    }
    
    juce::Logger::writeToLog("[WhisperThread] Profanity lexicon loaded successfully");
    std::cout << "[WhisperThread] Profanity lexicon loaded successfully" << std::endl;
    
    juce::Logger::writeToLog("[WhisperThread] About to set running flag and create thread");
    std::cout << "[WhisperThread] About to create processing thread..." << std::endl;
    running.store(true);
    
    juce::Logger::writeToLog("[WhisperThread] Creating std::thread...");
    try
    {
        processingThread = std::make_unique<std::thread>([this]() { 
            std::cout << "[WhisperThread] *** LAMBDA EXECUTING ***" << std::endl;
            juce::Logger::writeToLog("[WhisperThread] Lambda called, about to call run()");
            run(); 
        });
        std::cout << "[WhisperThread] Thread created successfully" << std::endl;
        juce::Logger::writeToLog("[WhisperThread] std::thread created");
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("[WhisperThread] EXCEPTION creating thread: " + juce::String(e.what()));
        running.store(false);
        return false;
    }
    
    juce::Logger::writeToLog("[WhisperThread] Started successfully");
    return true;
}

void WhisperThread::stop()
{
    if (!running.load())
        return;
    
    running.store(false);
    
    if (processingThread && processingThread->joinable())
    {
        processingThread->join();
    }
    
    juce::Logger::writeToLog("[WhisperThread] Stopped");
}

void WhisperThread::run()
{
    try
    {
        std::cout << "[WhisperThread] ====== RUN() CALLED ======" << std::endl;
        juce::Logger::writeToLog("[WhisperThread] ====== THREAD STARTED ======");
        juce::Logger::writeToLog("[WhisperThread] Processing loop started");
        std::cout << "[WhisperThread] Processing loop started" << std::endl;
        
        if (debugCallback)
        {
            debugCallback("[WhisperThread] Background thread is running");
        }
    }
    catch (...)
    {
        juce::Logger::writeToLog("[WhisperThread] CRASH during initial logging!");
        return;
    }
    
    int chunksProcessed = 0;
    auto lastDebugTime = std::chrono::steady_clock::now();
    
    // Accumulate audio for Whisper (process every ~1 second at 16kHz)
    const int targetSamples = WHISPER_SAMPLE_RATE; // 1 second at 16kHz
    std::vector<float> accumulatedAudio;
    accumulatedAudio.reserve(targetSamples * 2);
    
    std::cout << "[WhisperThread] Entering main processing loop..." << std::endl;
    std::cout << "[WhisperThread] Verifying pointers: audioQueue=" << audioQueue 
              << " censorQueue=" << censorQueue 
              << " circularBuffer=" << circularBuffer << std::endl;
    
    if (!audioQueue || !censorQueue || !circularBuffer)
    {
        std::cout << "[WhisperThread] ERROR: Null pointer detected!" << std::endl;
        return;
    }
    
    try
    {
        while (running.load())
        {
            try
            {
                auto chunkOpt = audioQueue->pop();
            
            static std::atomic<int> popCount{0};
            static std::atomic<int> successCount{0};
            int currentPop = popCount.fetch_add(1);
            
            // Debug: Verify queue pointer on first few iterations
            if (currentPop < 3)
            {
                std::cout << "[WhisperThread] Queue pointer: " << audioQueue << ", pop #" << currentPop << std::endl;
            }
            
            if (!chunkOpt.has_value() && currentPop % 200 == 0)
            {
                std::cout << "[WhisperThread] Still polling... (empty polls: " << currentPop << ", received: " << successCount.load() << ")" << std::endl;
            }
            
            if (chunkOpt.has_value())
            {
                successCount.fetch_add(1);
                if (successCount.load() <= 3)
                {
                    std::cout << "[WhisperThread] Chunk #" << successCount.load() << " received!" << std::endl;
                }
                
                const auto& chunk = chunkOpt.value();
                
                if (chunksProcessed == 0)
                {
                    std::cout << "[WhisperThread] FIRST AUDIO CHUNK: " << chunk.num_samples << " samples" << std::endl;
                    juce::Logger::writeToLog("[WhisperThread] First audio chunk received: " + 
                        juce::String(chunk.num_samples) + " samples");
                }
                
                // Read audio from CircularBuffer
                if (chunksProcessed == 0)
                    std::cout << "[WhisperThread] About to read from circular buffer..." << std::endl;
                
                juce::AudioBuffer<float> audioData;
                try
                {
                    // Pre-allocate buffer to avoid setSize() issues
                    audioData.setSize(chunk.num_channels, chunk.num_samples, false, true, false);
                    
                    if (chunksProcessed == 0)
                        std::cout << "[WhisperThread] Buffer pre-allocated, calling readSamplesAt..." << std::endl;
                    
                    if (!circularBuffer->readSamplesAt(audioData, chunk.buffer_position, chunk.num_samples))
                    {
                        juce::Logger::writeToLog("[WhisperThread] WARNING: Failed to read from circular buffer");
                        continue;
                    }
                }
                catch (const std::exception& e)
                {
                    std::cout << "[WhisperThread] EXCEPTION reading buffer: " << e.what() << std::endl;
                    continue;
                }
                catch (...)
                {
                    std::cout << "[WhisperThread] UNKNOWN EXCEPTION reading buffer" << std::endl;
                    continue;
                }
                
                // Validate audio data
                if (chunksProcessed == 0)
                    std::cout << "[WhisperThread] Buffer read OK, got " << audioData.getNumSamples() << " samples" << std::endl;
                    
                if (audioData.getNumSamples() == 0)
                {
                    juce::Logger::writeToLog("[WhisperThread] WARNING: Empty audio buffer");
                    continue;
                }
                
                // Downmix to mono
                if (chunksProcessed == 0)
                    std::cout << "[WhisperThread] About to downmix " << chunk.num_channels << " channels..." << std::endl;
                    
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
                
                // Convert to vector for resampling
                const float* samples = monoBuffer.getReadPointer(0);
                std::vector<float> monoData(samples, samples + chunk.num_samples);
                
                if (chunksProcessed == 0)
                {
                    juce::Logger::writeToLog("[WhisperThread] About to resample from " + 
                        juce::String(sampleRate) + " Hz to " + juce::String(WHISPER_SAMPLE_RATE) + " Hz");
                }
                
                // Resample to 16kHz if needed (Whisper requirement)
                if (sampleRate != WHISPER_SAMPLE_RATE)
                {
                    monoData = resampleTo16kHz(monoData);
                    
                    if (chunksProcessed == 0)
                    {
                        juce::Logger::writeToLog("[WhisperThread] Resampled to " + 
                            juce::String(monoData.size()) + " samples");
                    }
                }
                
                // Accumulate audio
                accumulatedAudio.insert(accumulatedAudio.end(), monoData.begin(), monoData.end());
                
                if (chunksProcessed % 10 == 0)
                {
                    juce::Logger::writeToLog("[WhisperThread] Accumulated: " + 
                        juce::String(accumulatedAudio.size()) + " / " + juce::String(targetSamples) + " samples");
                }
                
                // Process when we have enough audio
                if (accumulatedAudio.size() >= targetSamples)
                {
                    std::cout << "[WhisperThread] READY FOR INFERENCE: " << accumulatedAudio.size() << " samples" << std::endl;
                    
                    // Safety check
                    if (accumulatedAudio.empty() || !whisperCtx)
                    {
                        juce::Logger::writeToLog("[WhisperThread] ERROR: Invalid state for inference");
                        accumulatedAudio.clear();
                        continue;
                    }
                    
                    // Run Whisper inference
                    juce::Logger::writeToLog("[WhisperThread] Running inference on " + 
                        juce::String(accumulatedAudio.size()) + " samples");
                    
                    int result = -1;
                    try
                    {
                        result = whisper_full(whisperCtx, whisperParams, accumulatedAudio.data(), 
                                             static_cast<int>(accumulatedAudio.size()));
                    }
                    catch (const std::exception& e)
                    {
                        juce::Logger::writeToLog("[WhisperThread] EXCEPTION in whisper_full: " + juce::String(e.what()));
                        accumulatedAudio.clear();
                        continue;
                    }
                    catch (...)
                    {
                        juce::Logger::writeToLog("[WhisperThread] UNKNOWN EXCEPTION in whisper_full");
                        accumulatedAudio.clear();
                        continue;
                    }
                    
                    juce::Logger::writeToLog("[WhisperThread] Inference result: " + juce::String(result));
                    
                    if (result == 0)
                    {
                        // Get transcription
                        const int n_segments = whisper_full_n_segments(whisperCtx);
                        
                        for (int i = 0; i < n_segments; ++i)
                        {
                            const char* text = whisper_full_get_segment_text(whisperCtx, i);
                            if (text && strlen(text) > 0)
                            {
                                processTranscript(text, chunk.buffer_position);
                                
                                if (debugCallback)
                                    debugCallback("[Whisper] " + juce::String(text));
                            }
                        }
                    }
                    
                    // Clear accumulated audio (keep last 500ms for context at 16kHz)
                    if (accumulatedAudio.size() > WHISPER_SAMPLE_RATE / 2)
                    {
                        accumulatedAudio.erase(accumulatedAudio.begin(), 
                                              accumulatedAudio.begin() + (accumulatedAudio.size() - WHISPER_SAMPLE_RATE / 2));
                    }
                }
                
                chunksProcessed++;
                
                // Debug logging
                auto now = std::chrono::steady_clock::now();
                if (chunksProcessed % 100 == 0 || 
                    std::chrono::duration_cast<std::chrono::seconds>(now - lastDebugTime).count() >= 5)
                {
                    juce::String msg = "[Whisper] Processed " + juce::String(chunksProcessed) + " audio chunks";
                    juce::Logger::writeToLog(msg);
                    if (debugCallback)
                        debugCallback(msg);
                    lastDebugTime = now;
                }
            }
                if (!chunkOpt.has_value())
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }
            catch (const std::exception& e)
            {
                std::cout << "[WhisperThread] EXCEPTION in chunk processing: " << e.what() << std::endl;
                juce::Logger::writeToLog("[WhisperThread] Exception in chunk: " + juce::String(e.what()));
                // Continue processing
            }
            catch (...)
            {
                std::cout << "[WhisperThread] UNKNOWN EXCEPTION in chunk processing" << std::endl;
                juce::Logger::writeToLog("[WhisperThread] Unknown exception in chunk");
                // Continue processing
            }
        }
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("[WhisperThread] FATAL ERROR: " + juce::String(e.what()));
        if (debugCallback)
            debugCallback("[Whisper] FATAL ERROR: " + juce::String(e.what()));
    }
    
    juce::Logger::writeToLog("[WhisperThread] Processing thread stopped");
}

void WhisperThread::processTranscript(const char* text, int64_t bufferPosition)
{
    if (!text || strlen(text) == 0)
        return;
    
    juce::String transcript(text);
    
    // Tokenize transcript into words
    juce::StringArray tokens;
    tokens.addTokens(transcript, " \t\n", "");
    
    std::vector<ProfanityFilter::Word> words;
    words.reserve(tokens.size());
    
    // Convert to Word structures (simple tokenization, no timing info from Whisper segments)
    double timePerWord = 1.0 / std::max(1, tokens.size()); // Rough estimate
    for (int i = 0; i < tokens.size(); ++i)
    {
        ProfanityFilter::Word word;
        word.text = tokens[i].toLowerCase().toStdString();
        word.start_time = i * timePerWord;
        word.end_time = (i + 1) * timePerWord;
        words.push_back(word);
    }
    
    // Detect profanity
    auto profanitySpans = profanityFilter.detectProfanity(words);
    
    if (profanitySpans.empty())
        return;
    
    // Send censor events
    for (const auto& span : profanitySpans)
    {
        CensorEvent event;
        event.mode = CensorEvent::Mode::Reverse;
        
        // Estimate sample positions (rough approximation)
        // Whisper processes in ~1 second chunks
        event.start_sample = bufferPosition;
        event.end_sample = bufferPosition + (sampleRate / 2); // ~500ms span
        event.confidence = 1.0; // Whisper doesn't provide word-level confidence easily
        
        // Copy word (profanity detected)
        strncpy(event.word, span.text.c_str(), sizeof(event.word) - 1);
        event.word[sizeof(event.word) - 1] = '\0';
        
        if (!censorQueue->push(event))
        {
            juce::Logger::writeToLog("[WhisperThread] WARNING: Censor queue full");
        }
        else
        {
            if (debugCallback)
            {
                debugCallback("[Whisper] Profanity detected: \"" + juce::String(span.text) + "\" - CENSORING");
            }
        }
    }
}

std::vector<float> WhisperThread::resampleTo16kHz(const std::vector<float>& input)
{
    if (sampleRate == WHISPER_SAMPLE_RATE)
        return input;
    
    // Simple linear interpolation resampling
    const double ratio = static_cast<double>(WHISPER_SAMPLE_RATE) / static_cast<double>(sampleRate);
    const size_t outputSize = static_cast<size_t>(input.size() * ratio);
    
    std::vector<float> output;
    output.reserve(outputSize);
    
    for (size_t i = 0; i < outputSize; ++i)
    {
        const double srcPos = i / ratio;
        const size_t srcIndex = static_cast<size_t>(srcPos);
        
        if (srcIndex + 1 < input.size())
        {
            // Linear interpolation
            const double frac = srcPos - srcIndex;
            const float sample = input[srcIndex] * (1.0f - frac) + input[srcIndex + 1] * frac;
            output.push_back(sample);
        }
        else if (srcIndex < input.size())
        {
            output.push_back(input[srcIndex]);
        }
    }
    
    return output;
}
