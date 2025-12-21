#pragma once
#include <juce_core/juce_core.h>
#include <functional>
#include <thread>

// Forward declare socket type to avoid including winsock2.h here
typedef uintptr_t SOCKET;

class WebSocketServer {
public:
    using AudioCallback = std::function<void(const int16_t*, int)>;
    
    WebSocketServer(int port = 8765);
    ~WebSocketServer();
    
    bool start(AudioCallback callback);
    void stop();
    bool isRunning() const;
    bool hasClient() const;

private:
    void serverLoop();
    bool performHandshake();
    void handleClient();

    int port;
    bool running;
    SOCKET serverSocket;
    SOCKET clientSocket;
    std::thread serverThread;
    AudioCallback audioCallback;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WebSocketServer)
};
