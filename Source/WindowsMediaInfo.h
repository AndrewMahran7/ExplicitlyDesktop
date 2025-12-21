/*
  ==============================================================================

    WindowsMediaInfo.h
    Created: 20 Dec 2024
    Author: Explicitly Audio Systems

    Windows Media Control integration to get "Now Playing" information
    from media apps (Spotify, YouTube Music, Apple Music, etc.)

  ==============================================================================
*/

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <string>
#include <functional>

#ifdef _WIN32
#include <windows.h>
#include <wrl.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#endif

class WindowsMediaInfo
{
public:
    struct MediaInfo
    {
        std::string artist;
        std::string title;
        std::string album;
        bool isPlaying = false;
    };
    
    WindowsMediaInfo();
    ~WindowsMediaInfo();
    
    // Initialize Windows Media Control monitoring
    bool initialize();
    
    // Get current media info (non-blocking)
    MediaInfo getCurrentMedia();
    
    // Set callback for media changes
    void setMediaChangedCallback(std::function<void(const MediaInfo&)> callback);
    
private:
#ifdef _WIN32
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager sessionManager{nullptr};
    winrt::event_token sessionChangedToken;
    std::function<void(const MediaInfo&)> mediaCallback;
    
    void onMediaPropertiesChanged();
#endif
};
