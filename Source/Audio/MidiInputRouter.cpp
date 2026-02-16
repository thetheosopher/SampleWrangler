#include "MidiInputRouter.h"

namespace sw
{

    MidiInputRouter::MidiInputRouter() = default;

    MidiInputRouter::~MidiInputRouter()
    {
        if (attachedKeyboardState != nullptr)
        {
            attachedKeyboardState->removeListener(this);
            attachedKeyboardState = nullptr;
        }
        disableAllDevices();
    }

    void MidiInputRouter::enableDevice(const juce::String &deviceIdentifier)
    {
        auto devices = juce::MidiInput::getAvailableDevices();
        for (const auto &d : devices)
        {
            if (d.identifier == deviceIdentifier || d.name == deviceIdentifier)
            {
                if (auto input = juce::MidiInput::openDevice(d.identifier, this))
                {
                    input->start();
                    openInputs.push_back(std::move(input));
                }
                break;
            }
        }
    }

    void MidiInputRouter::disableAllDevices()
    {
        for (auto &input : openInputs)
        {
            if (input)
                input->stop();
        }
        openInputs.clear();
    }

    void MidiInputRouter::attachKeyboardState(juce::MidiKeyboardState &state)
    {
        if (attachedKeyboardState != nullptr)
            attachedKeyboardState->removeListener(this);

        attachedKeyboardState = &state;
        attachedKeyboardState->addListener(this);
    }

    void MidiInputRouter::setMidiCallback(MidiCallback cb)
    {
        onMidi = std::move(cb);
    }

    void MidiInputRouter::handleIncomingMidiMessage(juce::MidiInput * /*source*/,
                                                    const juce::MidiMessage &message)
    {
        if (onMidi)
            onMidi(message);
    }

    void MidiInputRouter::handleNoteOn(juce::MidiKeyboardState *, int midiChannel, int midiNoteNumber, float velocity)
    {
        if (!onMidi)
            return;

        const auto vel = static_cast<juce::uint8>(juce::jlimit(0, 127, static_cast<int>(velocity * 127.0f)));
        onMidi(juce::MidiMessage::noteOn(midiChannel, midiNoteNumber, vel));
    }

    void MidiInputRouter::handleNoteOff(juce::MidiKeyboardState *, int midiChannel, int midiNoteNumber, float velocity)
    {
        if (!onMidi)
            return;

        const auto vel = static_cast<juce::uint8>(juce::jlimit(0, 127, static_cast<int>(velocity * 127.0f)));
        onMidi(juce::MidiMessage::noteOff(midiChannel, midiNoteNumber, vel));
    }

} // namespace sw
