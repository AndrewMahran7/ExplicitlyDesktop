/*
  ==============================================================================

    AudioSourcePicker.cpp
    Created: 12 Dec 2024
    Author: Explicitly Audio Systems

    Implementation of WASAPI audio source enumeration and capture.

  ==============================================================================
*/

#include "AudioSourcePicker.h"
#include <iostream>
#include <tlhelp32.h>
#include <propvarutil.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "psapi.lib")

AudioSourcePicker::AudioSourcePicker()
    : deviceEnumerator(nullptr)
    , captureDevice(nullptr)
    , audioClient(nullptr)
    , captureClient(nullptr)
    , initialized(false)
    , capturing(false)
    , captureThread(nullptr)
    , shouldStopCapture(false)
{
}

AudioSourcePicker::~AudioSourcePicker()
{
    stopCapture();
    
    // Restore default device before cleanup
    restoreDefaultDevice();
    
    if (captureClient) captureClient->Release();
    if (audioClient) audioClient->Release();
    if (captureDevice) captureDevice->Release();
    if (deviceEnumerator) deviceEnumerator->Release();
    
    CoUninitialize();
}

bool AudioSourcePicker::initialize()
{
    if (initialized)
        return true;
    
    // Initialize COM
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
    {
        std::cout << "[AudioSourcePicker] Failed to initialize COM: " << std::hex << hr << std::endl;
        return false;
    }
    
    // Create device enumerator
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                         CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                         (void**)&deviceEnumerator);
    
    if (FAILED(hr))
    {
        std::cout << "[AudioSourcePicker] Failed to create device enumerator: " << std::hex << hr << std::endl;
        CoUninitialize();
        return false;
    }
    
    initialized = true;
    std::cout << "[AudioSourcePicker] Initialized successfully" << std::endl;
    
    return true;
}

void AudioSourcePicker::saveDefaultDevice()
{
    if (!deviceEnumerator)
        return;
    
    IMMDevice* defaultDevice = nullptr;
    HRESULT hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice);
    
    if (SUCCEEDED(hr))
    {
        LPWSTR deviceId = nullptr;
        hr = defaultDevice->GetId(&deviceId);
        
        if (SUCCEEDED(hr))
        {
            originalDefaultDeviceId = juce::String(deviceId);
            CoTaskMemFree(deviceId);
            std::cout << "[AudioSourcePicker] Saved default device: " << originalDefaultDeviceId << std::endl;
        }
        
        defaultDevice->Release();
    }
}

void AudioSourcePicker::restoreDefaultDevice()
{
    if (originalDefaultDeviceId.isEmpty())
        return;
    
    std::cout << "[AudioSourcePicker] Restoring default device: " << originalDefaultDeviceId << std::endl;
    
    // Note: Windows doesn't provide a direct API to set default device
    // The default device is typically managed by Windows Sound Settings
    // This is a limitation of the Windows Audio API
    // Users would need to manually restore or we'd need to use undocumented APIs
    
    // For now, just log that we attempted restoration
    // In a production app, you might want to use PolicyConfig interface (undocumented)
    // or simply inform the user that audio routing was changed
}

juce::String AudioSourcePicker::getProcessName(DWORD processId)
{
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
    if (!hProcess)
        return juce::String();
    
    WCHAR processName[MAX_PATH] = L"<unknown>";
    DWORD size = MAX_PATH;
    
    if (QueryFullProcessImageNameW(hProcess, 0, processName, &size))
    {
        juce::String fullPath(processName);
        CloseHandle(hProcess);
        return fullPath.fromLastOccurrenceOf("\\", false, false);
    }
    
    CloseHandle(hProcess);
    return juce::String();
}

juce::String AudioSourcePicker::getWindowTitle(DWORD processId)
{
    // Enumerate all windows to find main window for this process
    struct EnumData
    {
        DWORD processId;
        HWND hwnd;
    } data;
    
    data.processId = processId;
    data.hwnd = nullptr;
    
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL
    {
        auto* data = reinterpret_cast<EnumData*>(lParam);
        DWORD windowPid;
        GetWindowThreadProcessId(hwnd, &windowPid);
        
        if (windowPid == data->processId && IsWindowVisible(hwnd))
        {
            WCHAR title[256];
            if (GetWindowTextW(hwnd, title, 256) > 0)
            {
                data->hwnd = hwnd;
                return FALSE;  // Stop enumeration
            }
        }
        return TRUE;  // Continue enumeration
    }, reinterpret_cast<LPARAM>(&data));
    
    if (data.hwnd)
    {
        WCHAR title[256];
        if (GetWindowTextW(data.hwnd, title, 256) > 0)
            return juce::String(title);
    }
    
    return juce::String();
}

juce::String AudioSourcePicker::getBrowserTabTitle(const juce::String& processName, DWORD processId)
{
    // Check if it's a known browser
    if (processName.containsIgnoreCase("chrome") ||
        processName.containsIgnoreCase("edge") ||
        processName.containsIgnoreCase("firefox") ||
        processName.containsIgnoreCase("brave"))
    {
        // Get window title which usually includes tab title
        juce::String title = getWindowTitle(processId);
        
        if (title.isNotEmpty())
        {
            // Browser titles are usually "Page Title - Browser Name"
            // Extract just the page title
            int dashPos = title.lastIndexOf(" - ");
            if (dashPos > 0)
                return title.substring(0, dashPos);
        }
    }
    
    return juce::String();
}

bool AudioSourcePicker::enumerateAudioSessions(std::vector<AudioSource>& sources)
{
    if (!deviceEnumerator)
        return false;
    
    // Get default audio endpoint
    IMMDevice* device = nullptr;
    HRESULT hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    
    if (FAILED(hr))
    {
        std::cout << "[AudioSourcePicker] Failed to get default endpoint" << std::endl;
        return false;
    }
    
    // Get session manager
    IAudioSessionManager2* sessionManager = nullptr;
    hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL,
                         nullptr, (void**)&sessionManager);
    
    device->Release();
    
    if (FAILED(hr))
    {
        std::cout << "[AudioSourcePicker] Failed to get session manager" << std::endl;
        return false;
    }
    
    // Enumerate sessions
    IAudioSessionEnumerator* sessionEnum = nullptr;
    hr = sessionManager->GetSessionEnumerator(&sessionEnum);
    
    if (FAILED(hr))
    {
        sessionManager->Release();
        return false;
    }
    
    int sessionCount = 0;
    sessionEnum->GetCount(&sessionCount);
    
    for (int i = 0; i < sessionCount; ++i)
    {
        IAudioSessionControl* sessionControl = nullptr;
        hr = sessionEnum->GetSession(i, &sessionControl);
        
        if (FAILED(hr))
            continue;
        
        // Get extended session control
        IAudioSessionControl2* sessionControl2 = nullptr;
        hr = sessionControl->QueryInterface(__uuidof(IAudioSessionControl2),
                                           (void**)&sessionControl2);
        
        if (SUCCEEDED(hr))
        {
            // Get process ID
            DWORD processId = 0;
            sessionControl2->GetProcessId(&processId);
            
            if (processId != 0 && processId != GetCurrentProcessId())
            {
                AudioSource source;
                source.processId = processId;
                source.appName = getProcessName(processId);
                
                // Try to get browser tab title if it's a browser
                juce::String tabTitle = getBrowserTabTitle(source.appName, processId);
                if (tabTitle.isNotEmpty())
                {
                    source.windowTitle = tabTitle;
                    source.displayName = source.appName + " - " + tabTitle;
                }
                else
                {
                    source.windowTitle = getWindowTitle(processId);
                    if (source.windowTitle.isNotEmpty())
                        source.displayName = source.appName + " - " + source.windowTitle;
                    else
                        source.displayName = source.appName;
                }
                
                // Get volume
                ISimpleAudioVolume* volumeControl = nullptr;
                hr = sessionControl2->QueryInterface(__uuidof(ISimpleAudioVolume),
                                                    (void**)&volumeControl);
                if (SUCCEEDED(hr))
                {
                    float volume = 0.0f;
                    volumeControl->GetMasterVolume(&volume);
                    source.volume = volume;
                    volumeControl->Release();
                }
                
                // Check if active (playing audio)
                AudioSessionState state;
                sessionControl2->GetState(&state);
                source.isActive = (state == AudioSessionStateActive);
                
                if (source.appName.isNotEmpty())
                    sources.push_back(source);
            }
            
            sessionControl2->Release();
        }
        
        sessionControl->Release();
    }
    
    sessionEnum->Release();
    sessionManager->Release();
    
    return true;
}

std::vector<AudioSource> AudioSourcePicker::getActiveSources()
{
    std::vector<AudioSource> sources;
    
    if (!initialized)
    {
        std::cout << "[AudioSourcePicker] Not initialized, call initialize() first" << std::endl;
        return sources;
    }
    
    enumerateAudioSessions(sources);
    
    std::cout << "[AudioSourcePicker] Found " << sources.size() << " audio sources" << std::endl;
    for (const auto& source : sources)
    {
        std::cout << "  - " << source.displayName 
                  << " (PID: " << source.processId 
                  << ", Volume: " << source.volume 
                  << ", Active: " << (source.isActive ? "Yes" : "No") << ")" << std::endl;
    }
    
    return sources;
}

bool AudioSourcePicker::routeAppToVBCable(const AudioSource& source, const juce::String& vbCableDeviceName)
{
    if (!initialized || !deviceEnumerator)
        return false;
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "[AudioSourcePicker] ROUTING APP TO VB-CABLE" << std::endl;
    std::cout << "[AudioSourcePicker] App: " << source.displayName << std::endl;
    std::cout << "[AudioSourcePicker] Process ID: " << source.processId << std::endl;
    std::cout << "[AudioSourcePicker] Target Device: " << vbCableDeviceName << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    // Save original default device
    saveDefaultDevice();
    
    // Find VB-Cable device
    IMMDeviceCollection* deviceCollection = nullptr;
    HRESULT hr = deviceEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &deviceCollection);
    
    if (FAILED(hr))
    {
        std::cout << "[AudioSourcePicker] Failed to enumerate audio devices" << std::endl;
        return false;
    }
    
    UINT deviceCount = 0;
    deviceCollection->GetCount(&deviceCount);
    
    IMMDevice* vbCableDevice = nullptr;
    bool foundVBCable = false;
    
    for (UINT i = 0; i < deviceCount; i++)
    {
        IMMDevice* device = nullptr;
        hr = deviceCollection->Item(i, &device);
        
        if (SUCCEEDED(hr))
        {
            IPropertyStore* props = nullptr;
            hr = device->OpenPropertyStore(STGM_READ, &props);
            
            if (SUCCEEDED(hr))
            {
                PROPVARIANT varName;
                PropVariantInit(&varName);
                
                hr = props->GetValue(PKEY_Device_FriendlyName, &varName);
                
                if (SUCCEEDED(hr))
                {
                    juce::String deviceName = varName.pwszVal;
                    
                    std::cout << "[AudioSourcePicker] Found device: " << deviceName << std::endl;
                    
                    if (deviceName.contains("CABLE Input") || deviceName.contains("VB-Audio"))
                    {
                        std::cout << "[AudioSourcePicker] ✓ Found VB-Cable device!" << std::endl;
                        vbCableDevice = device;
                        foundVBCable = true;
                        PropVariantClear(&varName);
                        props->Release();
                        break;
                    }
                }
                
                PropVariantClear(&varName);
                props->Release();
            }
            
            if (!foundVBCable)
                device->Release();
        }
    }
    
    deviceCollection->Release();
    
    if (!foundVBCable || !vbCableDevice)
    {
        std::cout << "[AudioSourcePicker] ERROR: VB-Cable device not found!" << std::endl;
        std::cout << "[AudioSourcePicker] Make sure VB-Cable is installed and enabled" << std::endl;
        return false;
    }
    
    // Get VB-Cable device ID
    LPWSTR vbCableId = nullptr;
    hr = vbCableDevice->GetId(&vbCableId);
    
    if (FAILED(hr))
    {
        std::cout << "[AudioSourcePicker] Failed to get VB-Cable device ID" << std::endl;
        vbCableDevice->Release();
        return false;
    }
    
    std::wcout << L"[AudioSourcePicker] VB-Cable ID: " << vbCableId << std::endl;
    
    // IMPORTANT: Windows does NOT support per-application audio routing via API
    // The user must manually set the app's audio output to VB-Cable in Windows Sound Settings
    // OR use third-party tools like Audio Router or CheVolume
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "[AudioSourcePicker] MANUAL ROUTING REQUIRED" << std::endl;
    std::cout << "[AudioSourcePicker] Windows does not support programmatic per-app routing" << std::endl;
    std::cout << "[AudioSourcePicker] Please manually:" << std::endl;
    std::cout << "[AudioSourcePicker] 1. Right-click speaker icon → Open Sound Settings" << std::endl;
    std::cout << "[AudioSourcePicker] 2. Scroll to 'Advanced' → App volume and device preferences" << std::endl;
    std::cout << "[AudioSourcePicker] 3. Set '" << source.appName << "' output to 'CABLE Input'" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    CoTaskMemFree(vbCableId);
    vbCableDevice->Release();
    
    // Store the current source for restoration later
    currentSource = source;
    capturing = true;  // Mark as "active" so we know to restore later
    
    return true;
}

void AudioSourcePicker::restoreAppRouting()
{
    if (!capturing)
        return;
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "[AudioSourcePicker] RESTORING APP ROUTING" << std::endl;
    std::cout << "[AudioSourcePicker] App: " << currentSource.displayName << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    std::cout << "[AudioSourcePicker] Please manually restore '" << currentSource.appName 
              << "' output to default speakers in Windows Sound Settings" << std::endl;
    
    capturing = false;
    
    // Restore default system device
    restoreDefaultDevice();
    
    std::cout << "[AudioSourcePicker] App routing restoration complete" << std::endl;
}

// Keep old stopCapture for backward compatibility (just calls restoreAppRouting)
void AudioSourcePicker::stopCapture()
{
    restoreAppRouting();
}

void AudioSourcePicker::captureThreadFunction()
{
    // This would run in a separate thread to continuously capture audio
    // For now, this is a placeholder - actual implementation would need
    // to be integrated with your existing audio processing pipeline
    
    while (!shouldStopCapture && capturing)
    {
        UINT32 packetLength = 0;
        HRESULT hr = captureClient->GetNextPacketSize(&packetLength);
        
        if (FAILED(hr))
            break;
        
        if (packetLength > 0)
        {
            BYTE* data = nullptr;
            UINT32 numFrames = 0;
            DWORD flags = 0;
            
            hr = captureClient->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr);
            
            if (SUCCEEDED(hr))
            {
                // Process audio data here
                // This would call audioCallback with the captured audio
                
                captureClient->ReleaseBuffer(numFrames);
            }
        }
        
        juce::Thread::sleep(10);
    }
}
