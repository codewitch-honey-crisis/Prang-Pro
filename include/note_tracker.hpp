#pragma once
#include <stdint.h>
#include <sfx_midi_core.hpp>
#include <sfx_midi_message.hpp>
class note_tracker final {
    struct {
        uint32_t bank[4];
    } m_notes[16];
public:
    note_tracker();
    void process(const sfx::midi_message& message);
    void send_off(sfx::midi_output& output);
};