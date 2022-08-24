#pragma once
#include <Arduino.h>
#include <USBHost_t36.h>
#include <sfx.hpp>
class midi_in_teensy_usb_host final : public sfx::midi_input {
    bool m_initialized;
    USBHost m_usb_host;
    MIDIDevice m_in;
    sfx::midi_buffer32 m_buffer;
    void handle_message(const uint8_t* data,size_t size);
    static void handle_message_s(const uint8_t*data,size_t size,void* state);
public:
    inline midi_in_teensy_usb_host() : m_initialized(false), m_in(m_usb_host) {}
    virtual sfx::sfx_result receive(sfx::midi_message* out_message);
    sfx::sfx_result initialize();
    void update();
};
class midi_out_teensy_usb final : public sfx::midi_output {
    bool m_initialized;
public:
    inline midi_out_teensy_usb() : m_initialized(false) {
    }
    sfx::sfx_result initialize();
    virtual sfx::sfx_result send(const sfx::midi_message& message);
};