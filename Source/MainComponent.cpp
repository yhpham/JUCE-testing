#ifndef MAINCOMPONENT_H_INCLUDED
#define MAINCOMPONENT_H_INCLUDED

#include "../JuceLibraryCode/JuceHeader.h"
#include <iostream>
#include <vector>

struct SineWaveSound : public SynthesiserSound {
    SineWaveSound() {}
    
    bool appliesToNote (int /*midiNoteNumber*/) override        { return true; }
    bool appliesToChannel (int /*midiChannel*/) override        { return true; }
};

struct SineWaveVoice  : public SynthesiserVoice {
    SineWaveVoice()   : currentAngle (0), angleDelta (0), level (0), tailOff (0) {}
    
    bool canPlaySound (SynthesiserSound* sound) override {
        return dynamic_cast<SineWaveSound*> (sound) != nullptr;
    }
    
    void startNote (int midiNoteNumber, float velocity,
                    SynthesiserSound*, int /*currentPitchWheelPosition*/) override {
        currentAngle = 0.0;
        level = velocity * 0.15;
        tailOff = 0.0;
        
        double cyclesPerSecond = MidiMessage::getMidiNoteInHertz (midiNoteNumber);
        double cyclesPerSample = cyclesPerSecond / getSampleRate();
        
        angleDelta = cyclesPerSample * 2.0 * double_Pi;
    }
    
    void stopNote (float /*velocity*/, bool allowTailOff) override {
        if (allowTailOff) {
            if (tailOff == 0.0) {tailOff = 1.0;}
        }
        else {
            clearCurrentNote();
            angleDelta = 0.0;
        }
    }
    
    void pitchWheelMoved (int /*newValue*/) override { }
    
    void controllerMoved (int /*controllerNumber*/, int /*newValue*/) override { }
    
    void renderNextBlock (AudioSampleBuffer& outputBuffer, int startSample, int numSamples) override {
        if (angleDelta != 0.0) {
            if (tailOff > 0) {
                while (--numSamples >= 0) {
                    const float currentSample = (float) (std::sin (currentAngle) * level * tailOff);
                    
                    for (int i = outputBuffer.getNumChannels(); --i >= 0;)
                        outputBuffer.addSample (i, startSample, currentSample);
                    
                    currentAngle += angleDelta;
                    ++startSample;
                    
                    tailOff *= 0.99;
                    
                    if (tailOff <= 0.005) {
                        clearCurrentNote();
                        
                        angleDelta = 0.0;
                        break;
                    }
                }
            }
            else {
                while (--numSamples >= 0) {
                    const float currentSample = (float) (std::sin (currentAngle) * level);
                    
                    for (int i = outputBuffer.getNumChannels(); --i >= 0;)
                        outputBuffer.addSample (i, startSample, currentSample);
                    
                    currentAngle += angleDelta;
                    ++startSample;
                }
            }
        }
    }
    
private:
    double currentAngle, angleDelta, level, tailOff;
};

struct SynthAudioSource  : public AudioSource {
    SynthAudioSource (MidiKeyboardState& keyState)  : keyboardState (keyState) {
        for (int i = 4; --i >= 0;) {
            synth.addVoice (new SineWaveVoice());
        }
        
        setUsingSineWaveSound();
    }
    
    void setUsingSineWaveSound() {
        synth.clearSounds();
        synth.addSound (new SineWaveSound());
    }
    
    void prepareToPlay (int /*samplesPerBlockExpected*/, double sampleRate) override {
        midiCollector.reset (sampleRate);
        
        synth.setCurrentPlaybackSampleRate (sampleRate);
    }
    
    void releaseResources() override { }
    
    void getNextAudioBlock (const AudioSourceChannelInfo& bufferToFill) override {
        bufferToFill.clearActiveBufferRegion();
        
        MidiBuffer incomingMidi;
        midiCollector.removeNextBlockOfMessages (incomingMidi, bufferToFill.numSamples);
        
        keyboardState.processNextMidiBuffer (incomingMidi, 0, bufferToFill.numSamples, true);
        
        synth.renderNextBlock (*bufferToFill.buffer, incomingMidi, 0, bufferToFill.numSamples);
    }
    
    MidiMessageCollector midiCollector;
    MidiKeyboardState& keyboardState;
    
    Synthesiser synth;
};

class MainContentComponent  : public Component,
private ComboBox::Listener,
private MidiInputCallback,
private MidiKeyboardStateListener,
private Button::Listener {
public:
    MainContentComponent() : lastInputIndex (0),
    isAddingFromMidiInput (false),
    keyboardComponent (keyboardState, MidiKeyboardComponent::horizontalKeyboard),
    startTime (Time::getMillisecondCounterHiRes() * 0.001),
    synthAudioSource (keyboardState) {
        
        addAndMakeVisible (keyboardComponent);
        keyboardState.addListener (this);
        
        addAndMakeVisible (midiMessagesBox);
        midiMessagesBox.setMultiLine (true);
        
        addAndMakeVisible (recordButton);
        recordButton.setButtonText ("Record");
        recordButton.addListener (this);
        
        addAndMakeVisible (stopRecordButton);
        stopRecordButton.setButtonText ("Stop Recording");
        stopRecordButton.addListener (this);
        
        addAndMakeVisible (notesButton);
        notesButton.setButtonText ("Set notes");
        notesButton.setRadioGroupId (1);
        notesButton.addListener (this);
//        notesButton.setToggleState (true, dontSendNotification);
        
        addAndMakeVisible (rhythmButton);
        rhythmButton.setButtonText ("Set rhythm");
        rhythmButton.setRadioGroupId (1);
        rhythmButton.addListener (this);
        
        audioSourcePlayer.setSource (&synthAudioSource);
        deviceManager.addAudioCallback (&audioSourcePlayer);
        deviceManager.addMidiInputCallback (String(), &(synthAudioSource.midiCollector));
        
        setSize (600, 400);
    }
    
    ~MainContentComponent() {
        keyboardState.removeListener (this);
        audioSourcePlayer.setSource (nullptr);
        deviceManager.removeMidiInputCallback (String(), &(synthAudioSource.midiCollector));
        deviceManager.removeAudioCallback (&audioSourcePlayer);
    }
    
    void resized() override {
        Rectangle<int> area (getLocalBounds());
        keyboardComponent.setBounds (area.removeFromTop (80).reduced(8));
        midiMessagesBox.setBounds (area.reduced (8));
        
        recordButton.setBounds (16, 125, 150, 24);
        stopRecordButton.setBounds (16, 150, 150, 24);
        notesButton.setBounds (16, 175, 150, 24);
        rhythmButton.setBounds (16, 200, 150, 24);
    }
    
private:
    static String getMidiMessageDescription (const MidiMessage& m) {
        if (m.isNoteOn()) {
            return "Note on "  + MidiMessage::getMidiNoteName (m.getNoteNumber(), true, true, 3);
        }
        
        if (m.isNoteOff()) {
            return "Note off " + MidiMessage::getMidiNoteName (m.getNoteNumber(), true, true, 3);
        }
        
        return String::toHexString (m.getRawData(), m.getRawDataSize());
    }
    
    void logMessage (const String& m) {
        midiMessagesBox.moveCaretToEnd();
        midiMessagesBox.insertTextAtCaret (m + newLine);
    }
    
    void comboBoxChanged (ComboBox* box) override {}
    
    void handleIncomingMidiMessage (MidiInput* source, const MidiMessage& message) override {
        const ScopedValueSetter<bool> scopedInputFlag (isAddingFromMidiInput, true);
        keyboardState.processNextMidiEvent (message);
        postMessageToList (message, source->getName());
    }
    
    std::vector<String> notes;
    std::vector<String> times;
    
    void handleNoteOn (MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override {
        if (! isAddingFromMidiInput) {
            MidiMessage m (MidiMessage::noteOn (midiChannel, midiNoteNumber, velocity));
            m.setTimeStamp (Time::getMillisecondCounterHiRes() * 0.001);
            
            if (record and setNotes) {
                std::cout << MidiMessage::getMidiNoteName (m.getNoteNumber(), true, true, 3) << std::endl;
                notes.push_back(MidiMessage::getMidiNoteName (m.getNoteNumber(), true, true, 3));
            
                std::cout << "Notes so far: ";
                for (int i = 0; i < notes.size(); i++) {
                    std::cout << notes[i] << ", ";
                }
                std::cout << std::endl;
            }
            
            postMessageToList (m, "On-Screen Keyboard");
        }
    }
    
    void handleNoteOff (MidiKeyboardState*, int midiChannel, int midiNoteNumber, float /*velocity*/) override {
        if (! isAddingFromMidiInput) {
            MidiMessage m (MidiMessage::noteOff (midiChannel, midiNoteNumber));
            m.setTimeStamp (Time::getMillisecondCounterHiRes() * 0.001);
            postMessageToList (m, "On-Screen Keyboard");
        }
    }
    
    class IncomingMessageCallback   : public CallbackMessage {
    public:
        IncomingMessageCallback (MainContentComponent* o, const MidiMessage& m, const String& s)
        : owner (o), message (m), source (s)
        {}
        
        void messageCallback() override {
            if (owner != nullptr)
                owner->addMessageToList (message, source);
                }
        
        Component::SafePointer<MainContentComponent> owner;
        MidiMessage message;
        String source;
    };
    
    void postMessageToList (const MidiMessage& message, const String& source) {
        (new IncomingMessageCallback (this, message, source))->post();
    }
    
    void addMessageToList (const MidiMessage& message, const String& source) {
        const double time = message.getTimeStamp() - startTime;
        
        const int hours = ((int) (time / 3600.0)) % 24;
        const int minutes = ((int) (time / 60.0)) % 60;
        const int seconds = ((int) time) % 60;
        const int millis = ((int) (time * 1000.0)) % 1000;
        
        const String timecode (String::formatted ("%02d:%02d:%02d.%03d", hours, minutes, seconds, millis));
        
        
        if (record and setRhythm) {
            times.push_back(timecode);
            std::cout << timecode << std::endl;
        }
        
        const String description (getMidiMessageDescription (message));
        
        const String midiMessageString (timecode + "  -  " + description + " (" + source + ")");
//        logMessage (midiMessageString);
    }
    
    //==============================================================================
    AudioDeviceManager deviceManager;
    int lastInputIndex;
    bool isAddingFromMidiInput;
    
    MidiKeyboardState keyboardState;
    MidiKeyboardComponent keyboardComponent;
    
    TextEditor midiMessagesBox;
    double startTime;
    
    AudioSourcePlayer audioSourcePlayer;
    SynthAudioSource synthAudioSource;
    
    TextButton recordButton;
    TextButton stopRecordButton;
    ToggleButton notesButton;
    ToggleButton rhythmButton;
    bool record = false;
    bool setNotes = false;
    bool setRhythm = false;
    
    //==============================================================================
    void buttonClicked (Button* buttonThatWasClicked) {
        if (buttonThatWasClicked == &recordButton and record == false) {
            record = true;
        }
        else if (buttonThatWasClicked == &stopRecordButton and record == true) {
            record = false;
        }
        else if (buttonThatWasClicked == &notesButton) {
            setNotes = true;
            setRhythm = false;
        }
        else if (buttonThatWasClicked == &rhythmButton) {
            setNotes = false;
            setRhythm = true;
        }
    }
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainContentComponent);
};

Component* createMainContentComponent()     { return new MainContentComponent(); }


#endif  // MAINCOMPONENT_H_INCLUDED
