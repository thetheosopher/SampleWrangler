#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>

namespace sw
{

    /// Routes MIDI input from physical devices and the on-screen keyboard
    /// to the AudioEngine / VoiceManager for preview triggering.
    class MidiInputRouter final : public juce::MidiInputCallback,
                                  private juce::MidiKeyboardState::Listener
    {
    public:
        MidiInputRouter();
        ~MidiInputRouter() override;

        /// Start listening to the given MIDI input device.
        void enableDevice(const juce::String &deviceIdentifier);

        /// Stop listening to all MIDI inputs.
        void disableAllDevices();

        /// Connect the on-screen keyboard state so note events are forwarded.
        void attachKeyboardState(juce::MidiKeyboardState &state);

        /// Set a callback for incoming MIDI messages (called on MIDI thread).
        using MidiCallback = std::function<void(const juce::MidiMessage &)>;
        void setMidiCallback(MidiCallback cb);

        // --- MidiInputCallback ---
        void handleIncomingMidiMessage(juce::MidiInput *source,
                                       const juce::MidiMessage &message) override;

    private:
        void handleNoteOn(juce::MidiKeyboardState *, int midiChannel, int midiNoteNumber, float velocity) override;
        void handleNoteOff(juce::MidiKeyboardState *, int midiChannel, int midiNoteNumber, float velocity) override;

        MidiCallback onMidi;
        std::vector<std::unique_ptr<juce::MidiInput>> openInputs;
        juce::MidiKeyboardState *attachedKeyboardState = nullptr;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiInputRouter)
    };

} // namespace sw
