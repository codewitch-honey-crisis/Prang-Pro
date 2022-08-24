// SPI to use (we use the default one at index 0)
#define LCD_HOST 0
//
// WIRING
//
// LCD screen
#define LCD_CS 10
#define LCD_DC 8
#define LCD_RST 9
#define LCD_BKL 7
#define LCD_ROTATION 1
// wire the remaining LCD SPI
// pins to 
// MOSI (11)
// MISO (12) (where available)
// SCK (13)

// Encoder
#define ENC_CLK 24
#define ENC_DATA 25

// Momentary SPST buttons (open default)
#define BUTTON_A 26
#define BUTTON_B 27

// Break the USB host pins
// out to a USB-A port.

// Ideally you'd break the microUSB
// out to USB-B instead, but this
// isn't necessary.

#include <Arduino.h>
#include <Wire.h>
#include <sys/time.h>
#include <SD.h>
#include <tft_io.hpp>
#include <ili9341.hpp>
#include <Encoder.h>
#include <htcw_button.hpp>
#include <sfx.hpp>
#include <gfx_cpp14.hpp>
#include "midi_quantizer.hpp"
#include "midi_sampler.hpp"
#include "midi_teensy_usb.hpp"
#include "telegrama.hpp"
#include "PaulMaul.hpp"
#include "MIDI.hpp"

using namespace arduino;
using namespace sfx;
using namespace gfx;

struct midi_file_info final {
    int type;
    int tracks;
    int32_t microtempo;
};

using lcd_bus_t = tft_spi<LCD_HOST,LCD_CS>;
using lcd_t = ili9341<LCD_DC,LCD_RST,LCD_BKL,lcd_bus_t,LCD_ROTATION,true,400,200>;

using color_t = color<typename lcd_t::pixel_type>;

// stuff for making _gettimeofday() and std::chrono work
volatile uint64_t chrono_tick_count;
void chrono_tick() {
    ++chrono_tick_count;
}
IntervalTimer chrono_timer;

midi_in_teensy_usb_host midi_in;
midi_out_teensy_usb midi_out;

lcd_t lcd;

Encoder encoder(ENC_CLK,ENC_DATA);

button<BUTTON_A> button_a;
button<BUTTON_B> button_b;

midi_sampler sampler;
midi_quantizer quantizer;

midi_quantizer_timing last_timing;
uint32_t last_timing_ts;

File file;

midi_file_info file_info;

int base_octave;
int quantize_beats;
float tempo_multiplier;

uint32_t off_ts;
int64_t encoder_old_count;

sfx_result scan_file(File& file, midi_file_info* out_info) {
    midi_file mf;
    file_stream fs(file);
    size_t len = (size_t)fs.seek(0,seek_origin::end);
    fs.seek(0);
    uint8_t* buffer = (uint8_t*)malloc(len);
    if(buffer!=nullptr) {
        fs.read(buffer,len);
    }
    const_buffer_stream bs(buffer,len);
    stream& stm = buffer==nullptr?(stream&)fs:bs;
    sfx_result r = midi_file::read(stm, &mf);
    if (r != sfx_result::success) {
        if(buffer!=nullptr) {
            free(buffer);
        }
        return r;
    }
    out_info->tracks = (int)mf.tracks_size;
    int32_t file_mt = 500000;
    for (size_t i = 0; i < mf.tracks_size; ++i) {
        if (mf.tracks[i].offset != stm.seek(mf.tracks[i].offset)) {
            if(buffer!=nullptr) {
                free(buffer);
            }
            return sfx_result::end_of_stream;
        }
        bool found_tempo = false;
        int32_t mt = 500000;
        midi_event_ex me;
        me.absolute = 0;
        me.delta = 0;
        while (stm.seek(0, seek_origin::current) < mf.tracks[i].size) {
            size_t sz = midi_stream::decode_event(true, stm, &me);
            if (sz == 0) {
                if(buffer!=nullptr) {
                    free(buffer);
                }
                return sfx_result::unknown_error;
            }
            if (me.message.status == 0xFF && me.message.meta.type == 0x51) {
                int32_t mt2 = (me.message.meta.data[0] << 16) |
                              (me.message.meta.data[1] << 8) |
                              me.message.meta.data[2];
                if (!found_tempo) {
                    found_tempo = true;
                    mt = mt2;
                    file_mt = mt;
                } else {
                    if (mt != file_mt) {
                        mt = 0;
                        file_mt = 0;
                        break;
                    }
                    if (mt != mt2) {
                        mt = 0;
                        file_mt = 0;
                        break;
                    }
                }
            }
        }
    }
    out_info->microtempo = file_mt;
    out_info->type = mf.type;
    if(buffer!=nullptr) {
        free(buffer);
    }
    return sfx_result::success;
}

static void draw_error(const char* text) {
    draw::filled_rectangle(lcd, lcd.bounds(), color_t::white);
    
    float scale = PaulMaul.scale(60);
    ssize16 sz = PaulMaul.measure_text(ssize16::max(), spoint16::zero(), text, scale);
    srect16 rect = sz.bounds().center((srect16)lcd.bounds());
    draw::text(lcd, rect, spoint16::zero(), text, PaulMaul, scale, color_t::red, color_t::white, false);
}
void wait_and_restart() {
    button_a.update();
    button_b.update();
    while(!button_a.pressed() && !button_b.pressed()) {
        button_a.update();
        button_b.update();
        delay(1);
    }
    _reboot_Teensyduino_();
}
void update_tempo_mult() {
    sampler.tempo_multiplier(tempo_multiplier);
    char sz[32];
    sprintf(sz, "x%0.2f", tempo_multiplier);
    open_text_info oti;
    oti.font = &Telegrama_otf;
    oti.scale = Telegrama_otf.scale(25);
    oti.transparent_background = false;
    oti.text = sz;
    ssize16 tsz = Telegrama_otf.measure_text(ssize16::max(), spoint16::zero(), oti.text, oti.scale);
    srect16 trc = tsz.bounds();
    trc.offset_inplace(lcd.dimensions().width - tsz.width - 2, 2);
    draw::filled_rectangle(lcd, trc.inflate(100, 0), color_t::white);
    draw::text(lcd, trc, oti, color_t::black, color_t::white);  
}
void setup() {
    chrono_timer.begin(chrono_tick,1);
    bool reset_on_boot = false;
    off_ts = 0;
    encoder_old_count = 0;
    tempo_multiplier = 1.0;
    quantize_beats = 4;
    last_timing = midi_quantizer_timing::none;
    last_timing_ts = 0;
    Serial.println("Prang booting");
    Serial.begin(115200);
    if(true!=lcd.initialize()) {
        // the above weirdness is in case you switch drivers
        // - there's no standard for initialize() so different
        // drivers return different types, including gfx_result
        // gfx_result::success is 0 or false, so we can't compare
        // on that. This is better, but still not perfect.
        Serial.println("Unable to initialize LCD");
        while(true);
    }
    button_a.initialize();
    button_b.initialize();
    button_a.update();
    button_b.update();
    encoder.readAndReset();
    midi_in.initialize();
    midi_out.initialize();
    
    sampler.output(&midi_out);

    if(midi_quantizer::create(sampler,&quantizer)!=sfx_result::success) {
        Serial.println("Unable to create quantizer");
        while(true);
    }

    if (button_a.pressed() || button_b.pressed()) {
        reset_on_boot = true;
    }
    
    draw_error("inSerT SD c4rD");
    while (!SD.begin(BUILTIN_SDCARD)) {
        delay(25);
    }
restart:
    draw::filled_rectangle(lcd, lcd.bounds(), color_t::white);
    MIDI.seek(0);
    gfx_result gr = draw::image(lcd, rect16(0, 0, lcd.dimensions().width - 1, lcd.dimensions().height - 1), &MIDI);
    if (gr != gfx_result::success) {
        Serial.printf("Error loading MIDI.jpg (%d)\n", (int)gr);
        while (true)
            ;
    }
    float scale = PaulMaul.scale(lcd.dimensions().height / 1.3);
    static const char* prang_txt = "pr4nG";
    ssize16 txt_sz = PaulMaul.measure_text(ssize16::max(), spoint16::zero(), prang_txt, scale);
    srect16 txt_rct = txt_sz.bounds().center_horizontal((srect16)lcd.bounds());
    txt_rct.offset_inplace(0, lcd.dimensions().height - txt_sz.height);
    open_text_info pti;
    pti.font = &PaulMaul;
    pti.scale = scale;
    pti.text = prang_txt;
    pti.no_antialiasing = true;
    draw::text(lcd, txt_rct, pti, color_t::red);
    if (SD.totalSize() == 0) {
        draw_error("insert SD card");
        delay(10000);
        goto restart;
    }
    bool has_settings = false;
    if (SD.exists("/prang.csv")) {
        if (reset_on_boot) {
            SD.remove("/prang.csv");
        } else {
            file = SD.open("/prang.csv");
            base_octave = file.parseInt();
            if (',' == file.read()) {
                quantize_beats = file.parseInt();
            }
            file.close();
            has_settings = true;
        }
    }
    file = SD.open("/");
    size_t fn_count = 0;
    size_t fn_total = 0;
    while (true) {
        File f = file.openNextFile();
        if (!f) {
            break;
        }
        if (!f.isDirectory()) {
            const char* fn = f.name();
            size_t fnl = strlen(fn);
            if ((fnl > 5 && ((0 == strcmp(".midi", fn + fnl - 5) ||
                              (0 == strcmp(".MIDI", fn + fnl - 5) ||
                               (0 == strcmp(".Midi", fn + fnl - 5)))))) ||
                (fnl > 4 && ((0 == strcmp(".mid", fn + fnl - 4) ||
                             0 == strcmp(".MID", fn + fnl - 4)) ||
                 0 == strcmp(".Mid", fn + fnl - 4)))) {
                ++fn_count;
                fn_total += fnl + 1;
            }
        }
        f.close();
    }
    file.close();    
    char* fns = (char*)malloc(fn_total + 1) + 1;
    if(fn_total==0) {
        draw_error("no midi files");
        wait_and_restart();
    }
    if (fns == nullptr) {
        draw_error("too many files");
        wait_and_restart();
    }
    midi_file_info* mfs = (midi_file_info*)malloc(fn_total * sizeof(midi_file_info));
    if (mfs == nullptr) {
        draw_error("too many files");
        while (1)
            ;
    }
    float loading_scale = Telegrama_otf.scale(15);
    char loading_buf[64];
    sprintf(loading_buf, "loading file 0 of %d", (int)fn_count);
    ssize16 loading_size = Telegrama_otf.measure_text(ssize16::max(), spoint16::zero(), loading_buf, loading_scale);
    srect16 loading_rect = loading_size.bounds().center_horizontal((srect16)lcd.bounds()).offset(0, lcd.dimensions().height - loading_size.height);
    draw::text(lcd, loading_rect, spoint16::zero(), loading_buf, Telegrama_otf, loading_scale, color_t::blue, color_t::white, false);
    file = SD.open("/");
    char* str = fns;
    int fi = 0;
    int fli = 0;
    while (true) {
        File f = file.openNextFile();
        if (!f) {
            break;
        }
        if (!f.isDirectory()) {
            const char* fn = f.name();
            size_t fnl = strlen(fn);
            if ((fnl > 5 && ((0 == strcmp(".midi", fn + fnl - 5) ||
                              (0 == strcmp(".MIDI", fn + fnl - 5) ||
                               (0 == strcmp(".Midi", fn + fnl - 5)))))) ||
                (fnl > 4 && ((0 == strcmp(".mid", fn + fnl - 4) ||
                             0 == strcmp(".MID", fn + fnl - 4)) ||
                 0 == strcmp(".Mid", fn + fnl - 4)))) {
                ++fli;
                sprintf(loading_buf, "loading file %d of %d", fli, (int)fn_count);
                draw::filled_rectangle(lcd, loading_rect, color_t::white);
                loading_size = Telegrama_otf.measure_text(ssize16::max(), spoint16::zero(), loading_buf, loading_scale);
                loading_rect = loading_size.bounds().center_horizontal((srect16)lcd.bounds()).offset(0, lcd.dimensions().height - loading_size.height);
                draw::text(lcd, loading_rect, spoint16::zero(), loading_buf, Telegrama_otf, loading_scale, color_t::blue, color_t::white, false);
                if (sfx_result::success == scan_file(f, &mfs[fi])) {
                    memcpy(str, fn, fnl + 1);
                    str += fnl + 1;
                    ++fi;
                } else {
                    Serial.println("Failed to scan file");
                    --fn_count;
                }
            }
        }
        f.close();
    }
    file.close();
    draw::filled_rectangle(lcd, lcd.bounds(), color_t::white);

    base_octave = 4;
    tempo_multiplier = 1.0;

    char* curfn = fns;
    size_t fni = 0;
    const open_font& fnt = PaulMaul;
    if (fn_count > 1) {
        static const char* seltext = "select filE";
        float fscale = fnt.scale(50);
        ssize16 tsz = fnt.measure_text(ssize16::max(), spoint16::zero(), seltext, fscale);
        srect16 trc = tsz.bounds().center_horizontal((srect16)lcd.bounds());
        draw::text(lcd, trc.offset(0, 5), spoint16::zero(), seltext, fnt, fscale, color_t::red, color_t::white, false);
        fscale = Telegrama_otf.scale(12);
        bool done = false;
        int64_t ocount = encoder.read();
        button_a.update();
        button_b.update();
        int osw = button_a.pressed() || button_b.pressed();

        while (!done) {
            tsz = Telegrama_otf.measure_text(ssize16::max(), spoint16::zero(), curfn, fscale);
            trc = tsz.bounds().center_horizontal((srect16)lcd.bounds()).offset(0, 58);
            draw::filled_rectangle(lcd, srect16(0, trc.y1, lcd.dimensions().width - 1, trc.y2 + trc.height() + 5).inflate(100, 0), color_t::white);
            rgb_pixel<16> px = color_t::black;
            if (mfs[fni].type == 1) {
                px = color_t::blue;
            } else if (mfs[fni].type != 2) {
                px = color_t::red;
            }
            draw::text(lcd, trc, spoint16::zero(), curfn, Telegrama_otf, fscale, px, color_t::white, false);
            char szt[64];
            sprintf(szt, "%d tracks", (int)mfs[fni].tracks);
            tsz = Telegrama_otf.measure_text(ssize16::max(), spoint16::zero(), szt, fscale);
            trc = tsz.bounds().center_horizontal((srect16)lcd.bounds()).offset(0, 73);
            draw::text(lcd, trc, spoint16::zero(), szt, Telegrama_otf, fscale, color_t::black, color_t::white, false);
            int32_t mt = mfs[fni].microtempo;
            if (mt == 0) {
                strcpy(szt, "tempo: varies");
            } else {
                sprintf(szt, "tempo: %0.1f", midi_utility::microtempo_to_tempo(mt));
            }
            tsz = Telegrama_otf.measure_text(ssize16::max(), spoint16::zero(), szt, fscale);
            trc = tsz.bounds().center_horizontal((srect16)lcd.bounds()).offset(0, 88);
            draw::filled_rectangle(lcd, srect16(0, trc.y1, lcd.dimensions().width - 1, trc.y2 + trc.height() + 5).inflate(100, 0), color_t::white);
            draw::text(lcd, trc, spoint16::zero(), szt, Telegrama_otf, fscale, color_t::black, color_t::white, false);
            bool inc;
            
            while (ocount == (encoder.read()/4)) {
                button_a.update();
                button_b.update();
                int sw = button_a.pressed() || button_b.pressed();
                if (osw != sw && !sw) {
                    // button was released
                    done = true;
                    break;
                }
                osw = sw;
                delay(1);
            }
            if (!done) {
                int64_t count = (encoder.read()/4);
                inc = (ocount < count);
                ocount = count;
                if (inc) {
                    if (fni < fn_count - 1) {
                        ++fni;
                        curfn += strlen(curfn) + 1;
                    }
                } else {
                    if (fni > 0) {
                        --fni;
                        curfn = fns;
                        for (size_t j = 0; j < fni; ++j) {
                            curfn += strlen(curfn) + 1;
                        }
                    }
                }
            }
        }
    }
    encoder_old_count = encoder.read() / 4;
    Serial.print("File: ");
    Serial.println(curfn);
    --curfn;
    *curfn = '/';
    file = SD.open(curfn);
    if (!file) {
        draw_error("re-insert SD card");
        wait_and_restart();
    }
    file_info = mfs[fni];
    ::free(fns - 1);
    ::free(mfs);
    draw::filled_rectangle(lcd, lcd.bounds(), color_t::white);
    if (!has_settings) {
        static const char* oct_text = "base oct4vE";
        float fscale = fnt.scale(50);
        ssize16 tsz = fnt.measure_text(ssize16::max(), spoint16::zero(), oct_text, fscale);
        srect16 trc = tsz.bounds().center_horizontal((srect16)lcd.bounds());
        draw::text(lcd, trc.offset(0, 5), spoint16::zero(), oct_text, fnt, fscale, color_t::red, color_t::white, false);
        fscale = fnt.scale(50);
        bool done = false;
        int64_t ocount = encoder.read() / 4;
        button_a.update();
        button_b.update();
        int osw = button_a.pressed() || button_b.pressed();
        char sz[33];
        while (!done) {
            sprintf(sz, "%d", base_octave);
            fscale = Telegrama_otf.scale(25);
            tsz = Telegrama_otf.measure_text(ssize16::max(), spoint16::zero(), sz, fscale);
            trc = tsz.bounds().center_horizontal((srect16)lcd.bounds()).offset(0, 58);
            draw::filled_rectangle(lcd, srect16(0, trc.y1, lcd.dimensions().width - 1, trc.y2 + trc.height() + 5).inflate(100, 0), color_t::white);
            draw::text(lcd, trc, spoint16::zero(), sz, Telegrama_otf, fscale, color_t::black, color_t::white, false);

            bool inc;
            while (ocount == (encoder.read() / 4)) {
                button_a.update();
                button_b.update();
                int sw = button_a.pressed() || button_b.pressed();
                if (osw != sw && !sw) {
                    // button was released
                    done = true;
                    break;
                }
                osw = sw;
                delay(1);
            }
            if (!done) {
                int64_t count = (encoder.read() / 4);
                inc = (ocount < count);
                ocount = count;
                if (inc) {
                    if (base_octave < 10) {
                        ++base_octave;
                    }
                } else {
                    if (base_octave > 0) {
                        --base_octave;
                    }
                }
            }
        }
        draw::filled_rectangle(lcd, lcd.bounds(), color_t::white);
        static const char* qnt_text = "qu4ntiZE";
        fscale = fnt.scale(50);
        tsz = fnt.measure_text(ssize16::max(), spoint16::zero(), qnt_text, fscale);
        trc = tsz.bounds().center_horizontal((srect16)lcd.bounds());
        draw::text(lcd, trc.offset(0, 5), spoint16::zero(), qnt_text, fnt, fscale, color_t::red, color_t::white, false);
        fscale = fnt.scale(50);
        done = false;
        ocount = encoder.read() / 4;
        button_a.update();
        button_b.update();
        osw = button_a.pressed() || button_b.pressed();
        quantize_beats = 4;
        while (!done) {
            if (quantize_beats == 0) {
                strcpy(sz, "off");
            } else {
                sprintf(sz, "%d beats", quantize_beats);
            }
            fscale = Telegrama_otf.scale(25);
            tsz = Telegrama_otf.measure_text(ssize16::max(), spoint16::zero(), sz, fscale);
            trc = tsz.bounds().center_horizontal((srect16)lcd.bounds()).offset(0, 58);
            draw::filled_rectangle(lcd, srect16(0, trc.y1, lcd.dimensions().width - 1, trc.y2 + trc.height() + 5).inflate(100, 0), color_t::white);
            draw::text(lcd, trc, spoint16::zero(), sz, Telegrama_otf, fscale, color_t::black, color_t::white, false);

            bool inc;
            while (ocount == (encoder.read() / 4)) {
                button_a.update();
                button_b.update();
                int sw = button_a.pressed() || button_b.pressed();
                if (osw != sw && !sw) {
                    // button was released
                    done = true;
                    break;
                }
                osw = sw;
                delay(1);
            }
            if (!done) {
                int64_t count = (encoder.read() / 4);
                inc = (ocount < count);
                ocount = count;
                if (inc) {
                    if (quantize_beats < 16) {
                        ++quantize_beats;
                    }
                } else {
                    if (quantize_beats > 0) {
                        --quantize_beats;
                    }
                }
            }
        }
        draw::filled_rectangle(lcd, lcd.bounds(), color_t::white);
        static const char* save_text = "savE?";
        static const char* yes_text = "yes";
        static const char* no_text = "no";
        fscale = fnt.scale(50);
        tsz = fnt.measure_text(ssize16::max(), spoint16::zero(), save_text, fscale);
        trc = tsz.bounds().center((srect16)lcd.bounds());
        draw::text(lcd, trc, spoint16::zero(), save_text, fnt, fscale, color_t::red, color_t::white, false);
        fscale = Telegrama_otf.scale(25);
        tsz = Telegrama_otf.measure_text(ssize16::max(), spoint16::zero(), yes_text, fscale);
        trc = tsz.bounds();
        trc.offset_inplace(10, 0);
        open_text_info oti;
        oti.font = &Telegrama_otf;
        oti.transparent_background = false;
        oti.scale = fscale;
        oti.text = yes_text;
        draw::text(lcd, trc, oti, color_t::black, color_t::white);
        tsz = Telegrama_otf.measure_text(ssize16::max(), spoint16::zero(), no_text, fscale);
        trc = tsz.bounds();
        oti.text = no_text;
        trc.offset_inplace(10, lcd.dimensions().height - tsz.height);
        draw::text(lcd, trc, oti, color_t::black, color_t::white);
        int save = -1;
        while (button_a.pressed() || button_b.pressed()) {
            button_a.update();
            button_b.update();
        }
        while (0 > save) {
            button_a.update();
            button_b.update();
            if (button_a.pressed()) {
                save = 1;
            } else if (button_b.pressed()) {
                save = 0;
            }
            delay(1);
        }
        if (save) {
            if (SD.exists("/prang.csv")) {
                SD.remove("/prang.csv");
            }
            File file2 = SD.open("/prang.csv", O_WRITE);
            file2.print(base_octave);
            file2.print(",");
            file2.println(quantize_beats);
            file2.close();
        }
    }

    file_stream fs(file);
    sfx_result r = midi_sampler::read(fs, &sampler);
    if (r != sfx_result::success) {
        switch (r) {
            case sfx_result::out_of_memory:
                file.close();
                draw_error("file too big");
                delay(3000);
                goto restart;
            default:
                file.close();
                draw_error("not a MIDI file");
                delay(3000);
                goto restart;
        }
    }
    file.close();
    const char* playing_text = "pLay1nG";
    float playing_scale = fnt.scale(100);
    ssize16 playing_size = fnt.measure_text(ssize16::max(), spoint16::zero(), playing_text, playing_scale);
    draw::filled_rectangle(lcd, lcd.bounds(), color_t::white);
    draw::text(lcd, playing_size.bounds().center((srect16)lcd.bounds()), spoint16::zero(), playing_text, fnt, playing_scale, color_t::red, color_t::white, false);
    update_tempo_mult();
    r = midi_quantizer::create(sampler, &quantizer);
    if (r != sfx_result::success) {
        draw_error("file too big");
        delay(3000);
        goto restart;
    }
    quantizer.quantize_beats(quantize_beats);
    sampler.output(&midi_out);
    for (size_t i = 0; i < sampler.tracks_count(); ++i) {
        sampler.stop(i);
    }
    sampler.tempo_multiplier(tempo_multiplier);
    encoder_old_count = encoder.read() / 4;
    
    
}
void loop() {
    int64_t enc = encoder.read() / 4;
    if(encoder_old_count!=enc) {
        bool inc = encoder_old_count < enc;
        encoder_old_count=enc;
        if (inc) {
            if (tempo_multiplier < 4.99) {
                tempo_multiplier += .01;
                update_tempo_mult();
            }
        } else {
            if (tempo_multiplier > .01) {
                tempo_multiplier -= .01;
                update_tempo_mult();
            }
        }
    }
    int base_note = base_octave * 12;
    bool note_on = false;
    midi_in.update();
    midi_message msg;
    int note;
    int vel;
    if(midi_in.receive(&msg)==sfx_result::success) {
        switch (msg.type()) {
            case midi_message_type::note_on:
                note_on = true;
            case midi_message_type::note_off:
                Serial.println("Note");
                note = msg.msb();
                vel = msg.lsb();
                // is the note within our captured notes?
                if (/*(msg.channel()) == 0 && */
                    note >= base_note && 
                    note < base_note + (int)sampler.tracks_count()) {
                        Serial.println("trigger");
                    if (note_on && vel > 0) {
                        quantizer.start(note - base_note);
                        last_timing = quantizer.last_timing();
                        last_timing_ts = millis()+1000;
                        auto px = color_t::white;
                        if(last_timing==midi_quantizer_timing::exact) {
                            px = color_t::green;
                        } else if(last_timing==midi_quantizer_timing::early) {
                            px = color_t::blue;
                        } else if(last_timing==midi_quantizer_timing::late) {
                            px = color_t::red;
                        }   
                        draw::filled_ellipse(lcd,rect16(point16(0,0),16),px);
                    } else {
                        quantizer.stop(note - base_note);
                    }
                } else {
                    // just forward it
                    midi_out.send(msg);
                }
                break;
            case midi_message_type::polyphonic_pressure:
            case midi_message_type::control_change:
            case midi_message_type::pitch_wheel_change:
            case midi_message_type::song_position:
            case midi_message_type::program_change:
            case midi_message_type::channel_pressure:
            case midi_message_type::song_select:
            case midi_message_type::reset:
            case midi_message_type::system_exclusive:
            case midi_message_type::end_system_exclusive:
            case midi_message_type::active_sensing:
            case midi_message_type::start_playback:
            case midi_message_type::continue_playback:
            case midi_message_type::stop_playback:
            case midi_message_type::tune_request:
            case midi_message_type::timing_clock:
                midi_out.send(msg);
                break;
        }
    }
    sampler.update();
    if(last_timing_ts && millis()>=last_timing_ts) {
        last_timing_ts = 0;
        draw::filled_ellipse(lcd,rect16(point16(0,0),16),color_t::white);
    }
    
}
// implement _gettimeofday so std::chrono (used by SFX) works
extern "C" {
  int _gettimeofday( struct timeval *tv, void *tzvp )
  {
    // uptime in microseconds
    uint64_t t;
    noInterrupts();
    t=chrono_tick_count;
    interrupts();
    tv->tv_sec = t / 1000000;  // convert to seconds
    tv->tv_usec = ( t % 1000000 ) ;  // get remaining microseconds
    return 0;  // return non-zero for error
  } // end _gettimeofday()
}