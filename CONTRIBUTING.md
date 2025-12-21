# Contributing to Explicitly Desktop

Thank you for your interest in contributing! This document provides guidelines for contributing to the Explicitly Desktop project.

## Table of Contents

- [Getting Started](#getting-started)
- [Development Setup](#development-setup)
- [How to Contribute](#how-to-contribute)
- [Coding Standards](#coding-standards)
- [Testing](#testing)
- [Submitting Changes](#submitting-changes)

## Getting Started

1. **Fork the repository** on GitHub
2. **Clone your fork** locally:
   ```bash
   git clone https://github.com/YOUR_USERNAME/ExplicitlyDesktop.git
   cd ExplicitlyDesktop
   ```
3. **Add upstream remote**:
   ```bash
   git remote add upstream https://github.com/AndrewMahran7/ExplicitlyDesktop.git
   ```

## Development Setup

### Prerequisites

- **CMake** 3.20 or higher
- **Visual Studio 2019/2022** (Windows)
- **JUCE Framework** (included via submodule)
- **Whisper.cpp** (included in build)
- **VB-CABLE Virtual Audio Device** (download from https://vb-audio.com/Cable/)
- **Whisper Model**: ggml-tiny.en.bin

### Build Instructions

1. **Install VB-CABLE**:
   - Download from https://vb-audio.com/Cable/
   - Install and restart your computer

2. **Download Whisper Model**:
   - Download `ggml-tiny.en.bin` from Whisper.cpp releases
   - Place in `Models/` directory

3. **Initialize JUCE submodules**:
   ```powershell
   git submodule update --init --recursive
   ```

4. **Configure with CMake**:
   ```powershell
   mkdir build
   cd build
   cmake ..
   ```

5. **Build the project**:
   ```powershell
   cmake --build . --config Release
   ```

6. **Run the application**:
   ```powershell
   .\build\bin\Release\ExplicitlyDesktop.exe
   ```

### Project Structure

- `Source/` - Main application source code
  - `MainComponent.cpp/h` - UI and main application logic
  - `AudioEngine.cpp/h` - Core audio processing with 3-second delay buffer
  - `WhisperThread.cpp/h` - Whisper ASR integration
  - `VocalFilter.cpp/h` - Vocal isolation from music
  - `ProfanityFilter.h` - Lexicon-based profanity detection
  - `WebSocketServer.cpp/h` - WebSocket API for remote control
- `Models/` - Whisper models (ggml-tiny.en.bin)
- `lexicons/` - Profanity word lists (profanity_en.txt)
- `build/` - CMake build directory (git-ignored)

## How to Contribute

### Finding Issues to Work On

- Check the [Issues](https://github.com/AndrewMahran7/ExplicitlyDesktop/issues) page
- Look for issues labeled `good first issue` or `help wanted`
- Comment on an issue to let others know you're working on it

### Types of Contributions We Welcome

- **Bug fixes** - Fix crashes, audio glitches, or incorrect behavior
- **Performance improvements** - Optimize Whisper inference, reduce CPU usage
- **New features** - Faster ASR models, multi-language support, adjustable latency
- **Documentation** - Improve README, add code comments, write tutorials
- **Testing** - Add unit tests, report bugs with VB-CABLE setup details

## Coding Standards

### C++ Style Guide

- **Naming conventions**:
  - Classes: `PascalCase` (e.g., `AudioEngine`)
  - Functions/methods: `camelCase` (e.g., `processAudioBlock`)
  - Member variables: `camelCase` (e.g., `sampleRate`)
  - Constants: `UPPER_SNAKE_CASE` (e.g., `MAX_BUFFER_SIZE`)

- **Formatting**:
  - Indentation: 4 spaces (no tabs)
  - Braces: Allman style (opening brace on new line)
  - Line length: Max 120 characters

- **Best practices**:
  - Maintain thread safety in audio callback
  - Use appropriate synchronization for delay buffer access
  - Prefer JUCE idioms and classes where applicable
  - Comment complex algorithms, especially DSP, ASR, and vocal isolation
  - Test with VB-CABLE virtual audio device

### Commit Messages

- Use clear, descriptive commit messages
- Start with a verb in imperative mood (e.g., "Fix", "Add", "Update")
- Reference issue numbers when applicable

**Example**:
```
Fix audio buffer overflow in ASRThread (#42)

- Increase circular buffer size from 150ms to 300ms
- Add boundary checks in processAudioChunk()
- Resolves occasional pops when ASR processes slowly
```

## Testing

### Running Tests

```powershell
cd build
ctest --config Release
```

### Manual Testing

When submitting a PR, please test:
- VB-CABLE audio routing (system audio → CABLE Input → app input)
- Profanity filtering with music from Spotify/YouTube
- 3-second latency buffer behavior
- Whisper model loading and inference
- UI responsiveness
- Memory leaks (use Visual Studio diagnostic tools)
- CPU usage during Whisper processing

### Adding Tests

- Add unit tests for non-audio logic (profanity detection, parsing, etc.)
- For audio features, describe manual testing steps in your PR

## Submitting Changes

### Pull Request Process

1. **Create a feature branch**:
   ```bash
   git checkout -b feature/your-feature-name
   ```

2. **Make your changes**:
   - Write clean, well-commented code
   - Follow coding standards
   - Test thoroughly

3. **Commit your changes**:
   ```bash
   git add .
   git commit -m "Add feature: description"
   ```

4. **Push to your fork**:
   ```bash
   git push origin feature/your-feature-name
   ```

5. **Open a Pull Request** on GitHub:
   - Provide a clear description of changes
   - Reference related issues
   - Include screenshots/videos for UI changes
   - List testing steps performed

### PR Review Process

- A maintainer will review your PR within 1-2 weeks
- Address any requested changes
- Once approved, your PR will be merged

## Questions?

- Open an [Issue](https://github.com/AndrewMahran7/ExplicitlyDesktop/issues) for questions
- Join our community discussions
- Email: andrew.mahran7@gmail.com

## Code of Conduct

Please read our [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) before contributing.

---

**Thank you for contributing to Explicitly Desktop!** 🎵
