#include "note_tracker.hpp"
#include <string.h>
note_tracker::note_tracker() {
    memset(m_notes,0,sizeof(m_notes));
}
void note_tracker::process(const sfx::midi_message& message) {
    sfx::midi_message_type t = message.type();
    uint8_t c = message.channel();
    uint8_t n = message.msb();
    size_t b = n /32;
    size_t bi = n % 32;
    if(t==sfx::midi_message_type::note_off || 
            (t==sfx::midi_message_type::note_on &&
                    message.lsb()==0)) {
        const uint64_t mask = uint64_t(~(1<<bi));    
        m_notes[c].bank[b]&=mask;
        
    } else if(t==sfx::midi_message_type::note_on) {
        const uint64_t set = uint64_t(1<<(bi));    
        m_notes[c].bank[b]|=set;
        
    }
}
void note_tracker::send_off(sfx::midi_output& output) {
    for(int i = 0;i<16;++i) {
        for(int j=0;j<4;++j) {
            for(int k=0;k<32;++k) {
                const uint64_t mask = uint64_t(1<<k);
                if(m_notes[i].bank[j]&mask) {
                    sfx::midi_message msg;
                    msg.status = uint8_t(uint8_t(sfx::midi_message_type::note_off)|uint8_t(i));
                    msg.msb(k+(j*32));
                    msg.lsb(0);
                    output.send(msg);
                }
            }
        }
    }
    memset(m_notes,0,sizeof(m_notes));
}