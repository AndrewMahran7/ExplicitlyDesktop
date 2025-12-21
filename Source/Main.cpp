/*
  ==============================================================================

    Main.cpp
    Created: 9 Dec 2024
    Author: Explicitly Audio Systems

    Application entry point for the Explicitly Desktop ultra-low latency profanity filter.
    
    This application captures audio from input device, processes it through
    streaming ASR (Vosk) for profanity detection, and outputs filtered audio
    with <150ms total latency. NO vocal separation, NO heavy ML models.
    
    Purpose: Latency testing harness for embedded hardware feasibility study.

  ==============================================================================
*/

#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"

//==============================================================================
/**
    Main application class that manages the lifecycle of the Explicitly Desktop app.
    
    This handles:
    - Application initialization and shutdown
    - Main window creation and management
    - Global application settings
    - Exception handling and crash recovery
*/
class ExplicitlyDesktopApplication  : public juce::JUCEApplication
{
public:
    //==============================================================================
    ExplicitlyDesktopApplication() {}

    const juce::String getApplicationName() override       
    { 
        // Create startup log
        juce::File logFile = juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
            .getChildFile("ExplicitlyStartup.log");
        logFile.appendText("Application getName() called\n");
        
        return "Explicitly Desktop"; 
    }
    
    const juce::String getApplicationVersion() override    
    { 
        return "1.0.0"; 
    }
    
    bool moreThanOneInstanceAllowed() override             
    { 
        // Only allow one instance to prevent audio device conflicts
        return false; 
    }

    //==============================================================================
    /**
        Initialize the application and create the main window.
        
        This is called once when the application starts. It:
        - Creates the main application window
        - Initializes audio system
        - Loads ML models (if configured)
        - Sets up global exception handlers
    */
    void initialise (const juce::String& commandLine) override
    {
        juce::File logFile = juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
            .getChildFile("ExplicitlyStartup.log");
        logFile.appendText("initialise() started\n");
        
        // Print startup banner
        juce::Logger::writeToLog("================================================================================");
        juce::Logger::writeToLog(" Explicitly Desktop - Real-Time Profanity Filter");
        juce::Logger::writeToLog(" Version: " + getApplicationVersion());
        juce::Logger::writeToLog("================================================================================");
        juce::Logger::writeToLog("");

        logFile.appendText("Creating main window\n");
        
        // Create main application window
        mainWindow.reset(new MainWindow(getApplicationName()));
        
        logFile.appendText("Main window created successfully\n");
        
        juce::Logger::writeToLog("[Main] Application initialized successfully");
        juce::Logger::writeToLog("[Main] Main window created");
    }

    /**
        Shutdown the application and cleanup resources.
        
        This is called when the application is about to quit. It:
        - Stops audio processing
        - Releases audio devices
        - Saves user settings
        - Cleanup ML models
    */
    void shutdown() override
    {
        juce::Logger::writeToLog("[Main] Application shutting down...");
        
        // Destroy main window (this will trigger proper cleanup)
        mainWindow = nullptr;
        
        juce::Logger::writeToLog("[Main] Shutdown complete");
    }

    //==============================================================================
    /**
        Handle system quit request (user closing app or system shutdown).
    */
    void systemRequestedQuit() override
    {
        juce::Logger::writeToLog("[Main] System requested quit");
        quit();
    }

    /**
        Handle another instance attempting to start (blocked by moreThanOneInstanceAllowed).
    */
    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        juce::Logger::writeToLog("[Main] Another instance attempted to start - blocked");
        
        // Bring existing window to front
        if (mainWindow != nullptr)
        {
            mainWindow->toFront(true);
        }
    }

    //==============================================================================
    /**
        Main application window that hosts the audio processing GUI.
        
        This is a DocumentWindow that contains the MainComponent with all
        audio controls, visualizations, and settings.
    */
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (juce::String name)
            : DocumentWindow (name,
                            juce::Desktop::getInstance().getDefaultLookAndFeel()
                                .findColour (juce::ResizableWindow::backgroundColourId),
                            DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);

           #if JUCE_IOS || JUCE_ANDROID
            setFullScreen (true);
           #else
            // Set reasonable window size for desktop
            setResizable (true, true);
            centreWithSize (getWidth(), getHeight());
           #endif

            setVisible (true);
            
            juce::Logger::writeToLog("[MainWindow] Window created and visible");
        }

        /**
            Handle window close button click.
            
            This triggers application shutdown.
        */
        void closeButtonPressed() override
        {
            juce::Logger::writeToLog("[MainWindow] Close button pressed");
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
/**
    This macro generates the main() routine that launches the app.
    
    JUCE handles all platform-specific entry point details, so we just
    need to provide our application class.
*/
START_JUCE_APPLICATION (ExplicitlyDesktopApplication)
