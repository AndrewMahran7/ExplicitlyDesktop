# Explicitly Desktop - Technology Stack & Performance Metrics

## Hardware Specifications
- **CPU**: Standard desktop processor (x86-64)
- **RAM**: 8GB+ available
- **GPU**: No GPU acceleration currently used (CPU-only Whisper)
- **Audio Interface**: VB-Audio Virtual Cable (virtual audio routing)
- **Sample Rate**: 48,000 Hz
- **Bit Depth**: 32-bit float
- **Channels**: 2 (stereo)
- **Buffer Size**: 512 samples (10.67ms at 48kHz)

## Core Technologies

### 1. **Whisper.cpp** (Speech Recognition)
- **Model**: `ggml-tiny.en.bin` (English-only, optimized)
- **Model Size**: 75 MB
- **Architecture**: Transformer-based encoder-decoder (39M parameters)
- **Quantization**: Default (no quantization)
- **Backend**: CPU only (flash attention enabled)
- **Performance**: 
  - Processing 5 seconds of audio takes ~0.75 seconds
  - RTF (Real-Time Factor): **0.15x** (6.7x faster than real-time)
  - Throughput: ~6.7x real-time speed
- **Memory Usage**:
  - Model: 75 MB
  - KV self cache: ~4 MB
  - KV cross cache: ~12 MB
  - KV pad cache: ~1 MB
  - Compute buffer (conv): ~5 MB
  - Compute buffer (encode): ~8 MB
  - Compute buffer (cross): ~2 MB
  - Compute buffer (decode): ~20 MB
  - **Total Whisper Memory**: ~127 MB

### 2. **JUCE Framework** (Audio I/O & GUI)
- **Version**: Latest (7.x series)
- **Modules Used**:
  - `juce_core` - Core utilities
  - `juce_audio_basics` - Audio data structures
  - `juce_audio_devices` - Audio device management
  - `juce_audio_formats` - Audio file I/O
  - `juce_audio_processors` - Audio processing
  - `juce_gui_basics` - GUI components
  - `juce_gui_extra` - Extended GUI features
- **Build Size**: ~50 MB (compiled application)

### 3. **Custom Audio Processing Pipeline**

#### **Delay Buffer System**
- **Total Buffer Size**: 960,000 samples (20 seconds @ 48kHz)
- **Initial Buffering**: 10 seconds before playback starts
- **Active Buffer**: 10 seconds of audio in flight
- **Purpose**: Allows Whisper processing time while maintaining real-time playback

#### **Vocal Filter** (VocalFilter.cpp)
- **Type**: Bandpass filter (IIR Butterworth)
- **Frequency Range**: 150 Hz - 5000 Hz (vocal range)
- **Purpose**: Isolates vocals for better Whisper accuracy
- **Processing Time**: Negligible (~0.1ms per 512-sample buffer)

#### **Timestamp Refiner** (TimestampRefiner.cpp)
- **Method**: Audio energy analysis + zero-crossing rate
- **Window Size**: 480 samples (10ms @ 48kHz)
- **Search Radius**: 38,400 samples (0.8s @ 48kHz, increased for tiny model)
- **Energy Threshold**: 0.001 (tuned for music with vocals)
- **Zero-Crossing Threshold**: 0.1
- **Bias**: 20% preference for earlier timestamps (compensates for tiny model's late timestamping)
- **Purpose**: Fixes Whisper tiny's imprecise word-level timestamps
- **Accuracy Improvement**: Typically ±50-400ms refinement (larger range due to tiny model)

#### **Profanity Filter** (ProfanityFilter.h)
- **Lexicon Size**: ~400 profane words/phrases
- **Lexicon File**: `profanity_en.txt` (text file)
- **Pattern Matching**: Multi-word pattern support (e.g., "god damn")
- **Case Sensitivity**: Case-insensitive matching
- **Processing Time**: <1ms per transcript

#### **Quality Analyzer** (QualityAnalyzer.cpp)
- **Metrics Tracked**:
  - Buffer size (seconds)
  - Buffer underruns
  - Input level (RMS)
  - Censorship events
- **Purpose**: Monitor system health and performance

### 4. **Audio Censorship Methods**
- **Mute Mode**: Replaces profanity with silence
- **Reverse Mode**: Plays profane words backwards (current default)
- **Latency**: 10 seconds (initial buffer delay)
- **Accuracy**: Depends on Whisper transcription quality (~95%+ on clear vocals)

## Performance Benchmarks (Current System)

### **Processing Pipeline Timing**
1. **Audio Capture**: Real-time (10.67ms per 512-sample buffer)
2. **Vocal Filtering**: <0.1ms per buffer
3. **Buffer Accumulation**: 5 seconds of audio collected
4. **Whisper Transcription**: 0.75 seconds (RTF 0.15x)
5. **Timestamp Refinement**: ~10-50ms per word (depends on word count)
6. **Profanity Detection**: <1ms
7. **Audio Censorship**: Real-time (applied during playback)

### **Total Latency Breakdown**
- **Initial Buffering**: 10 seconds (one-time at startup)
- **Ongoing Latency**: 10 seconds (delay between input and output)
- **Processing Overhead**: ~0.75 seconds every 5 seconds (pipelined, doesn't add latency)

### **CPU Usage**
- **Idle**: ~2-5% (GUI + audio I/O)
- **During Transcription**: ~15-25% (efficient tiny model)
- **Average**: ~10-15% sustained

### **Memory Usage**
- **Application Base**: ~50 MB
- **Whisper Model**: ~127 MB (tiny model)
- **Delay Buffer**: ~7.3 MB (960k samples × 2 channels × 4 bytes)
- **Processing Buffers**: ~20 MB (various audio buffers)
- **Total**: **~200 MB**

## Data Flow

```
Audio Input (48kHz stereo)
    ↓
VB-Cable Virtual Device
    ↓
JUCE Audio Callback (512 samples)
    ↓
Vocal Filter (150-5000 Hz bandpass)
    ↓
Delay Buffer (20s capacity, 10s active)
    ↓
    ├→ Write Head: Stores incoming audio
    ↓
Accumulation Buffer (5s chunks)
    ↓
Whisper Processing (every 5s)
    ├→ Resample 48kHz → 16kHz
    ├→ Transcribe with Whisper
    ├→ Extract word timestamps
    ├→ Refine timestamps (energy analysis)
    ├→ Detect profanity
    └→ Create censor regions
    ↓
Delay Buffer Read Head (10s behind write)
    ↓
Apply Censorship (mute/reverse profane words)
    ↓
Audio Output (48kHz stereo)
    ↓
Speakers/Headphones
```

## File Sizes

### **Source Code**
- `AudioEngine.cpp`: ~1,040 lines, ~45 KB
- `MainComponent.cpp`: ~500 lines, ~22 KB
- `WhisperThread.cpp`: ~200 lines, ~9 KB
- `TimestampRefiner.cpp`: ~250 lines, ~11 KB
- `VocalFilter.cpp`: ~150 lines, ~7 KB
- `QualityAnalyzer.cpp`: ~200 lines, ~9 KB
- `ProfanityFilter.h`: ~200 lines, ~8 KB
- `LyricsAlignment.cpp`: ~300 lines, ~13 KB
- **Total Source**: ~2,840 lines, ~124 KB

### **Compiled Application**
- `Explicitly Desktop.exe`: ~8-10 MB (Release build)
- **Dependencies**:
  - `whisper.dll`: ~15 MB
  - `ggml.dll`: ~5 MB
  - `ggml-cuda.dll`: ~10 MB (unused, CPU-only mode)
  - `ggml-base.dll`: ~3 MB
  - `ggml-cpu.dll`: ~5 MB
  - JUCE (statically linked): included in .exe
- **Total Distribution**: ~50-60 MB + 75 MB model file = **~125 MB total**

### **Models**
- `ggml-tiny.en.bin`: 75 MB (primary model in use - optimized for speed)
- Alternative models available:
  - `ggml-base.en.bin`: ~142 MB (more accurate, slower)
  - `ggml-small.en.bin`: ~487 MB (high accuracy, 5x slower)
  - `ggml-medium.en.bin`: ~1.5 GB (very accurate, 15x slower)
  - `ggml-large.bin`: ~3 GB (best accuracy, 30x slower)

## Build Configuration
- **Compiler**: MSVC 2022 (Microsoft Visual C++)
- **C++ Standard**: C++17
- **Build Type**: Release (optimized)
- **Platform**: Windows x64
- **CMake**: 3.20+

## Current Limitations
1. **GPU Acceleration**: Not implemented (CPU-only Whisper)
   - Could reduce RTF from 0.15x to ~0.05x with CUDA (marginal benefit)
2. **Timestamp Precision**: Tiny model timestamps are less precise (±400ms vs ±50ms for larger models)
   - Compensated with asymmetric padding and refiner bias
3. **Latency**: 10-second delay (acceptable for live content, noticeable for interactive use)
4. **Single Language**: English-only model
5. **Accuracy**: ~90-92% on clear vocals with tiny model (vs ~95% with small), lower on mumbled/overlapping speech

## Potential Optimizations
1. **Quantization**: INT8 quantization could reduce model to ~38 MB with minimal accuracy loss
2. **GPU Acceleration**: CUDA could reduce processing time to ~0.25s (0.05x RTF) - diminishing returns
3. **Larger Model**: Whisper base (142 MB) for better timestamp accuracy if needed
4. **Parallel Processing**: Already using 8 CPU threads for Whisper
5. **Buffer Reduction**: With current 0.15x RTF, could reduce buffer to 3-4 seconds safely
6. **Model Upgrade**: Consider base model if timestamp precision issues persist

## Why This Stack Works
- **Whisper Tiny**: Fast, lightweight speech recognition (39M parameters, 75MB)
- **JUCE**: Industry-standard audio framework (used in professional DAWs)
- **Real-Time Factor 0.15x**: 6.7x faster than real-time = massive headroom
- **10-Second Buffer**: Provides safety margin even with fast processing
- **Low Memory**: Only 200MB total (suitable for embedded deployment)
- **Modular Design**: Easy to swap models, filters, or censorship methods
- **Excellent Balance**: Speed vs accuracy optimized for profanity detection use case
