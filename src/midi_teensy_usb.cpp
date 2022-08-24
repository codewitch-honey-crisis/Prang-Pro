#include <midi_teensy_usb.hpp>
#include <sfx.hpp>
using namespace sfx;
sfx_result midi_in_teensy_usb_host::initialize() {
    if(!m_initialized) {
        m_usb_host.begin();
        m_in.setHandleMessage(handle_message_s,this);
        m_initialized = true;
    }
    return sfx::sfx_result::success;
}
void midi_in_teensy_usb_host::update() {
    m_usb_host.Task();
    m_in.read();
}
void midi_in_teensy_usb_host::handle_message(const uint8_t* data,size_t size) {
    midi_event_ex e;
    const_buffer_stream cbs(data,size);
    midi_stream::decode_message(false,cbs,&e.message);
    m_buffer.put(e);
}
void midi_in_teensy_usb_host::handle_message_s(const uint8_t* data,size_t size,void* state) {
    midi_in_teensy_usb_host* This = (midi_in_teensy_usb_host*)state;
    This->handle_message(data,size);
}
sfx_result midi_in_teensy_usb_host::receive(midi_message* out_message) {
    if(m_buffer.empty()) {
        return sfx::sfx_result::end_of_stream;
    }
    midi_event_ex e;
    if(!m_buffer.get(&e)) {
        return sfx::sfx_result::end_of_stream;
    }
    *out_message = e.message;
    return sfx_result::success;
}
sfx_result midi_out_teensy_usb::initialize() {
    if(!m_initialized) {
        usbMIDI.begin();
        m_initialized = true;
    }
    return sfx_result::success;
}
sfx_result midi_out_teensy_usb::send(const midi_message& msg) {
    switch(msg.type()) {
        case midi_message_type::note_off:
        case midi_message_type::note_on:
        case midi_message_type::polyphonic_pressure:
        case midi_message_type::control_change:
        case midi_message_type::pitch_wheel_change:
            usbMIDI.send((int)msg.type(), msg.msb(),msg.lsb(),msg.channel()+1,0);
            break;
        case midi_message_type::song_position:
        case midi_message_type::program_change:
        case midi_message_type::channel_pressure:
        case midi_message_type::song_select:
            usbMIDI.send((int)msg.type(), msg.msb(),0,msg.channel()+1,0);
            break;
        case midi_message_type::system_exclusive:
            usbMIDI.sendSysEx(msg.sysex.size,msg.sysex.data,false,0);
            break;
        case midi_message_type::reset:
        case midi_message_type::end_system_exclusive:
        case midi_message_type::active_sensing:
        case midi_message_type::start_playback:
        case midi_message_type::stop_playback:
        case midi_message_type::tune_request:
        case midi_message_type::timing_clock:
            usbMIDI.send((int)msg.type(), 0,0,msg.channel()+1,0);
            break;
        default:
            return sfx_result::invalid_format;
    }
    return sfx_result::success;
}