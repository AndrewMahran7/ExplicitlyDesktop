cmake_minimum_required(VERSION 3.20)

project(ExplicitlyDesktop VERSION 1.0.0)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE Path (adjust if needed)
set(JUCE_DIR "C:/JUCE" CACHE PATH "Path to JUCE framework")

if(NOT EXISTS "${JUCE_DIR}/modules")
    message(FATAL_ERROR "JUCE not found at ${JUCE_DIR}. Please set JUCE_DIR to your JUCE installation path.")
endif()

# Add JUCE to the build
add_subdirectory(${JUCE_DIR} JUCE EXCLUDE_FROM_ALL)

# Whisper SDK Path (adjust if needed)
set(WHISPER_SDK_DIR "C:/whisper-sdk" CACHE PATH "Path to Whisper SDK")

if(NOT EXISTS "${WHISPER_SDK_DIR}/include/whisper.h")
    message(FATAL_ERROR "Whisper SDK not found at ${WHISPER_SDK_DIR}. Please see WHISPER_SETUP.md")
endif()

# Include directories
include_directories(${WHISPER_SDK_DIR}/include)

# Source files
set(SOURCES
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/AudioEngine.cpp
    Source/WhisperThread.cpp
    Source/LyricsAlignment.cpp
    Source/VocalFilter.cpp
    Source/TimestampRefiner.cpp
    Source/QualityAnalyzer.cpp
    Source/SongRecognition.cpp
    Source/WindowsMediaInfo.cpp
)

# Create executable
juce_add_gui_app(ExplicitlyDesktop
    PRODUCT_NAME "Explicitly Desktop"
    COMPANY_NAME "Explicitly Audio Systems"
    BUNDLE_ID "com.explicitly.desktop"
    ICON_BIG ""
)

# Add source files
target_sources(ExplicitlyDesktop PRIVATE ${SOURCES})

# Add header files
target_include_directories(ExplicitlyDesktop PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

# Link JUCE modules
target_link_libraries(ExplicitlyDesktop
    PRIVATE
        juce::juce_core
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Link Whisper library
if(WIN32)
    # Use CUDA-enabled whisper build for GPU acceleration
    set(WHISPER_CUDA_BUILD "C:/whisper.cpp/build-cuda")
    
    target_link_libraries(ExplicitlyDesktop PRIVATE
        ${WHISPER_CUDA_BUILD}/src/Release/whisper.lib
        windowsapp.lib  # Windows Runtime for Media Control
    )
    
    # Copy CUDA-enabled Whisper DLLs to output directory
    add_custom_command(TARGET ExplicitlyDesktop POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${WHISPER_CUDA_BUILD}/bin/Release/whisper.dll"
            $<TARGET_FILE_DIR:ExplicitlyDesktop>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${WHISPER_CUDA_BUILD}/bin/Release/ggml.dll"
            $<TARGET_FILE_DIR:ExplicitlyDesktop>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${WHISPER_CUDA_BUILD}/bin/Release/ggml-cuda.dll"
            $<TARGET_FILE_DIR:ExplicitlyDesktop>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${WHISPER_CUDA_BUILD}/bin/Release/ggml-base.dll"
            $<TARGET_FILE_DIR:ExplicitlyDesktop>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${WHISPER_CUDA_BUILD}/bin/Release/ggml-cpu.dll"
            $<TARGET_FILE_DIR:ExplicitlyDesktop>
        COMMENT "Copying CUDA-enabled Whisper DLLs to output directory"
    )
    
    # Copy CUDA runtime DLLs for GPU acceleration
    set(CUDA_TOOLKIT_ROOT "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.6")
    if(EXISTS "${CUDA_TOOLKIT_ROOT}/bin")
        add_custom_command(TARGET ExplicitlyDesktop POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CUDA_TOOLKIT_ROOT}/bin/cudart64_12.dll"
                $<TARGET_FILE_DIR:ExplicitlyDesktop>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CUDA_TOOLKIT_ROOT}/bin/cublas64_12.dll"
                $<TARGET_FILE_DIR:ExplicitlyDesktop>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CUDA_TOOLKIT_ROOT}/bin/cublasLt64_12.dll"
                $<TARGET_FILE_DIR:ExplicitlyDesktop>
            COMMENT "Copying CUDA runtime DLLs for GPU support"
        )
    endif()
elseif(APPLE)
    target_link_libraries(ExplicitlyDesktop PRIVATE
        ${WHISPER_SDK_DIR}/lib/libwhisper.dylib
    )
elseif(UNIX)
    target_link_libraries(ExplicitlyDesktop PRIVATE
        ${WHISPER_SDK_DIR}/lib/libwhisper.so
    )
endif()

# Copy Models folder to output directory
add_custom_command(TARGET ExplicitlyDesktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/Models
        $<TARGET_FILE_DIR:ExplicitlyDesktop>/Models
    COMMENT "Copying Models folder to output directory"
)

# Copy lexicons folder to output directory
add_custom_command(TARGET ExplicitlyDesktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:ExplicitlyDesktop>/lexicons
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/Models/profanity_en.txt
        $<TARGET_FILE_DIR:ExplicitlyDesktop>/lexicons/profanity_en.txt
    COMMENT "Copying lexicons to output directory"
)

# Compiler definitions
target_compile_definitions(ExplicitlyDesktop
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ExplicitlyDesktop,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ExplicitlyDesktop,JUCE_VERSION>"
)

# Windows-specific settings
if(WIN32)
    target_compile_definitions(ExplicitlyDesktop PRIVATE
        _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
    )
endif()

# Output directory
set_target_properties(ExplicitlyDesktop PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Phase 3: Standalone Whisper test executable
add_executable(WhisperTest Source/WhisperTest.cpp)

target_include_directories(WhisperTest PRIVATE
    ${WHISPER_SDK_DIR}/include
)

if(WIN32)
    target_link_libraries(WhisperTest PRIVATE
        ${WHISPER_SDK_DIR}/lib/whisper.lib
    )
    
    # Copy Whisper DLLs to WhisperTest output directory
    add_custom_command(TARGET WhisperTest POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${WHISPER_SDK_DIR}/lib
            $<TARGET_FILE_DIR:WhisperTest>
        COMMENT "Copying Whisper DLLs to WhisperTest directory"
    )
endif()

# Copy Models folder to WhisperTest output directory
add_custom_command(TARGET WhisperTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/Models
        $<TARGET_FILE_DIR:WhisperTest>/Models
    COMMENT "Copying Models folder to WhisperTest directory"
)

set_target_properties(WhisperTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

message(STATUS "===========================================")
message(STATUS "Explicitly Desktop Configuration")
message(STATUS "===========================================")
message(STATUS "JUCE Directory: ${JUCE_DIR}")
message(STATUS "Whisper SDK Directory: ${WHISPER_SDK_DIR}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Targets: ExplicitlyDesktop, WhisperTest")
message(STATUS "==========================================")
