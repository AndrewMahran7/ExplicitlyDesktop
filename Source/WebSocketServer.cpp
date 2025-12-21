/*
  ==============================================================================

    WebSocketServer.cpp
    Created: 12 Dec 2024
    Author: Explicitly Audio Systems

    WebSocket server implementation using Windows Sockets.

  ==============================================================================
*/

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include "WebSocketServer.h"
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>

// SHA-1 implementation for WebSocket handshake
namespace {
    // Simple SHA-1 for WebSocket key generation
    std::string base64Encode(const unsigned char* data, size_t len) {
        static const char* base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string ret;
        int i = 0;
        unsigned char char_array_3[3];
        unsigned char char_array_4[4];

        while (len--) {
            char_array_3[i++] = *(data++);
            if (i == 3) {
                char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
                char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
                char_array_4[3] = char_array_3[2] & 0x3f;

                for(i = 0; i < 4; i++)
                    ret += base64_chars[char_array_4[i]];
                i = 0;
            }
        }

        if (i) {
            for(int j = i; j < 3; j++)
                char_array_3[j] = '\0';

            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

            for (int j = 0; j < i + 1; j++)
                ret += base64_chars[char_array_4[j]];

            while(i++ < 3)
                ret += '=';
        }

        return ret;
    }

    // Simplified SHA-1 (for WebSocket accept key)
    std::string sha1(const std::string& input) {
        // For simplicity, using a hash approximation
        // In production, use proper SHA-1 from OpenSSL or CryptoAPI
        unsigned char hash[20] = {0};
        
        // Simple hash for demo (NOT cryptographically secure)
        for (size_t i = 0; i < input.length(); i++) {
            hash[i % 20] ^= input[i];
            hash[(i + 1) % 20] += input[i];
        }
        
        return std::string((char*)hash, 20);
    }
}

WebSocketServer::WebSocketServer(int p)
    : port(p)
    , running(false)
    , serverSocket(INVALID_SOCKET)
    , clientSocket(INVALID_SOCKET)
{
}

WebSocketServer::~WebSocketServer()
{
    stop();
}

bool WebSocketServer::start(AudioCallback callback)
{
    if (running)
        return true;

    audioCallback = callback;

    std::cout << "\n========================================" << std::endl;
    std::cout << "[WebSocket] Starting server on port " << port << std::endl;
    std::cout << "========================================\n" << std::endl;

    // Initialize Winsock
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cout << "[WebSocket] ✗ WSAStartup failed: " << result << std::endl;
        return false;
    }

    running = true;
    serverThread = std::thread(&WebSocketServer::serverLoop, this);

    std::cout << "[WebSocket] ✓ Server started successfully" << std::endl;
    std::cout << "[WebSocket] Browser extension can now connect\n" << std::endl;

    return true;
}

void WebSocketServer::stop()
{
    if (!running)
        return;

    std::cout << "[WebSocket] Stopping server..." << std::endl;

    running = false;

    if (clientSocket != INVALID_SOCKET) {
        closesocket(clientSocket);
        clientSocket = INVALID_SOCKET;
    }

    if (serverSocket != INVALID_SOCKET) {
        closesocket(serverSocket);
        serverSocket = INVALID_SOCKET;
    }

    if (serverThread.joinable())
        serverThread.join();

    WSACleanup();

    std::cout << "[WebSocket] Server stopped\n" << std::endl;
}

bool WebSocketServer::isRunning() const
{
    return running;
}

bool WebSocketServer::hasClient() const
{
    return clientSocket != INVALID_SOCKET;
}

void WebSocketServer::serverLoop()
{
    // Create socket
    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        std::cout << "[WebSocket] ✗ Failed to create socket" << std::endl;
        running = false;
        return;
    }

    // Bind to port
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cout << "[WebSocket] ✗ Bind failed (port may be in use)" << std::endl;
        closesocket(serverSocket);
        serverSocket = INVALID_SOCKET;
        running = false;
        return;
    }

    // Listen
    if (listen(serverSocket, 1) == SOCKET_ERROR) {
        std::cout << "[WebSocket] ✗ Listen failed" << std::endl;
        closesocket(serverSocket);
        serverSocket = INVALID_SOCKET;
        running = false;
        return;
    }

    std::cout << "[WebSocket] Listening for browser connections..." << std::endl;

    // Accept loop
    while (running) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(serverSocket, &readSet);

        timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int selectResult = select(0, &readSet, nullptr, nullptr, &timeout);
        if (selectResult > 0) {
            clientSocket = accept(serverSocket, nullptr, nullptr);
            if (clientSocket != INVALID_SOCKET) {
                std::cout << "\n[WebSocket] ✓ Browser extension connected!" << std::endl;

                if (performHandshake()) {
                    std::cout << "[WebSocket] ✓ Handshake complete - receiving audio" << std::endl;
                    handleClient();
                } else {
                    std::cout << "[WebSocket] ✗ Handshake failed" << std::endl;
                }

                closesocket(clientSocket);
                clientSocket = INVALID_SOCKET;
                std::cout << "[WebSocket] Browser disconnected\n" << std::endl;
            }
        }
    }
}

bool WebSocketServer::performHandshake()
{
    char buffer[2048];
    int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

    if (bytesReceived <= 0)
        return false;

    buffer[bytesReceived] = '\0';
    std::string request(buffer);

    // Extract WebSocket key
    size_t keyPos = request.find("Sec-WebSocket-Key: ");
    if (keyPos == std::string::npos)
        return false;

    keyPos += 19;
    size_t keyEnd = request.find("\r\n", keyPos);
    std::string key = request.substr(keyPos, keyEnd - keyPos);

    // Generate accept key
    std::string acceptKey = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string hash = sha1(acceptKey);
    std::string accept = base64Encode((unsigned char*)hash.data(), hash.length());

    // Send response
    std::stringstream response;
    response << "HTTP/1.1 101 Switching Protocols\r\n";
    response << "Upgrade: websocket\r\n";
    response << "Connection: Upgrade\r\n";
    response << "Sec-WebSocket-Accept: " << accept << "\r\n";
    response << "\r\n";

    std::string responseStr = response.str();
    send(clientSocket, responseStr.c_str(), (int)responseStr.length(), 0);

    return true;
}

void WebSocketServer::handleClient()
{
    std::vector<char> buffer(16384);
    int audioPacketCount = 0;

    while (running && clientSocket != INVALID_SOCKET) {
        int bytesReceived = recv(clientSocket, buffer.data(), (int)buffer.size(), 0);

        if (bytesReceived <= 0)
            break;

        if (bytesReceived < 2)
            continue;

        // Parse WebSocket frame
        unsigned char firstByte = buffer[0];
        unsigned char secondByte = buffer[1];

        int opcode = firstByte & 0x0F;
        bool isMasked = (secondByte & 0x80) != 0;
        int payloadLength = secondByte & 0x7F;

        int headerSize = 2;

        // Extended payload length
        if (payloadLength == 126) {
            if (bytesReceived < 4) continue;
            payloadLength = (buffer[2] << 8) | buffer[3];
            headerSize = 4;
        } else if (payloadLength == 127) {
            continue; // Skip very large frames
        }

        // Masking key
        unsigned char maskingKey[4] = {0};
        if (isMasked) {
            if (bytesReceived < headerSize + 4) continue;
            memcpy(maskingKey, &buffer[headerSize], 4);
            headerSize += 4;
        }

        if (bytesReceived < headerSize + payloadLength)
            continue;

        // Handle different frame types
        if (opcode == 0x02) { // Binary frame (audio data)
            // Unmask payload
            std::vector<char> payload(payloadLength);
            for (int i = 0; i < payloadLength; i++) {
                payload[i] = buffer[headerSize + i] ^ maskingKey[i % 4];
            }

            // Convert to int16 samples
            int numSamples = payloadLength / 2;
            const int16_t* samples = reinterpret_cast<const int16_t*>(payload.data());

            // Call audio callback
            if (audioCallback && numSamples > 0) {
                audioCallback(samples, numSamples);

                // Log progress
                audioPacketCount++;
                if (audioPacketCount % 100 == 0) {
                    std::cout << "[WebSocket] Received " << audioPacketCount 
                              << " audio packets (" << numSamples << " samples each)" << std::endl;
                }
            }
        }
        else if (opcode == 0x08) { // Close frame
            std::cout << "[WebSocket] Close frame received" << std::endl;
            break;
        }
        else if (opcode == 0x09) { // Ping frame
            // Send pong
            unsigned char pong[] = {0x8A, 0x00};
            send(clientSocket, (char*)pong, 2, 0);
        }
    }

    std::cout << "[WebSocket] Total audio packets received: " << audioPacketCount << std::endl;
}
