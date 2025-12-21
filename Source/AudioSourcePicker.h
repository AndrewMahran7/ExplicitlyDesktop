/*
  ==============================================================================

    AudioSourcePicker.h
    Created: 12 Dec 2024
    Author: Explicitly Audio Systems

    WASAPI-based audio source enumeration and capture.
    Allows capturing audio from specific applications without virtual cables.

  ==============================================================================
*/

#pragma once

#include <vector>
#include <string>
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <psapi.h>
#include <juce_core/juce_core.h>

// Audio source information
struct AudioSource
{
    juce::String appName;           // e.g., "Microsoft Edge"
    juce::String windowTitle;       // e.g., "YouTube Music"
    DWORD processId;                // Process ID for targeting
    float volume;                   // Current volume level (0.0-1.0)
    bool isActive;                  // Currently playing audio
    juce::String displayName;       // Combined display name for UI
    
    AudioSource() : processId(0), volume(0.0f), isActive(false) {}
};

class AudioSourcePicker
{
public:
    AudioSourcePicker();
    ~AudioSourcePicker();
    
    /**
        Initialize WASAPI COM interfaces.
        Must be called before any other methods.
        
        @return true if initialization successful
    */
    bool initialize();
    
    /**
        Enumerate all applications currently playing audio.
        
        @return Vector of audio sources with process info
    */
    std::vector<AudioSource> getActiveSources();
    
    /**
        Route a specific application's audio to VB-Cable.
        This redirects the app's output to VB-Cable so it can be captured normally.
        
        @param source               The audio source to route
        @param vbCableDeviceName   Name of VB-Cable device (e.g., "CABLE Input (VB-Audio Virtual Cable)")
        @return                     true if routing successful
    */
    bool routeAppToVBCable(const AudioSource& source, const juce::String& vbCableDeviceName);
    
    /**
        Restore the routed application back to default speakers.
    */
    void restoreAppRouting();
    
    /**
        Legacy method - calls restoreAppRouting().
    */
    void stopCapture();
    
    /**
        Check if currently capturing.
    */
    bool isCapturing() const { return capturing; }
    
    /**
        Get current capture source info.
    */
    AudioSource getCurrentSource() const { return currentSource; }
    
    /**
        Save the current default audio device before making changes.
    */
    void saveDefaultDevice();
    
    /**
        Restore the original default audio device.
        Call this when app closes to revert audio routing.
    */
    void restoreDefaultDevice();

private:
    IMMDeviceEnumerator* deviceEnumerator;
    IMMDevice* captureDevice;
    IAudioClient* audioClient;
    IAudioCaptureClient* captureClient;
    
    bool initialized;
    bool capturing;
    AudioSource currentSource;
    std::function<void(const float*, int, int)> audioCallback;
    
    juce::Thread* captureThread;
    std::atomic<bool> shouldStopCapture;
    
    // Store original default device for restoration
    juce::String originalDefaultDeviceId;
    
    /**
        Get process name from process ID.
    */
    juce::String getProcessName(DWORD processId);
    
    /**
        Get main window title for a process.
    */
    juce::String getWindowTitle(DWORD processId);
    
    /**
        Check if process is a browser and get active tab title.
    */
    juce::String getBrowserTabTitle(const juce::String& processName, DWORD processId);
    
    /**
        Background thread for audio capture.
    */
    void captureThreadFunction();
    
    /**
        Helper to enumerate all audio sessions.
    */
    bool enumerateAudioSessions(std::vector<AudioSource>& sources);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSourcePicker)
};
