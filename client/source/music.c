/*
 * Background music. MUSIC_PATH is IMA-ADPCM that the sound hardware plays and
 * loops by itself (see tools/make_music.py for the format), so once started
 * it needs no CPU time and keeps going through network calls and downloads.
 *
 * The file is loaded whole (about 550 KB for the DSi Shop theme), which fits
 * next to everything else in a DS Lite's 4 MB. It isn't built into the .nds,
 * so the shop can be shared without Nintendo's music, and it can be swapped.
 */
#include <nds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "music.h"

#define MUSIC_CH  0

static u8  *g_data;
static bool g_playing;
static bool g_lid_closed;
static bool g_resume_after_lid;
static unsigned g_vol;
static unsigned g_timer, g_loop_word, g_words;
static void play(void);

static void play(void) {
    soundPreparePcm(MUSIC_CH | SOUND_START, g_vol, 64, g_timer, SoundMode_Repeat,
                    SoundFmt_ImaAdpcm, g_data, g_loop_word, g_words - g_loop_word);
    g_playing = true;
}

void music_start(const Config *config) {
    if (!config->music || g_data) return;

    FILE *f = fopen(MUSIC_PATH, "rb");
    if (!f) return;
    u32 hdr[4];                    /* 'DSMU', rate, loop word, data size */
    if (fread(hdr, sizeof(hdr), 1, f) != 1 || memcmp(hdr, "DSMU", 4) != 0
        || hdr[1] < 1000 || hdr[3] < 8 || hdr[3] % 4 || hdr[2] >= hdr[3] / 4) {
        fclose(f);
        return;
    }
    g_data = malloc(hdr[3]);
    if (g_data && fread(g_data, hdr[3], 1, f) != 1) {
        free(g_data);
        g_data = NULL;
    }
    fclose(f);
    if (!g_data) return;

    /* the ARM7 reads the samples straight from main RAM */
    DC_FlushRange(g_data, hdr[3]);

    g_timer = soundTimerFromHz(hdr[1]);
    g_loop_word = hdr[2];
    g_words = hdr[3] / 4;
    g_vol = (unsigned)config->music_volume * 2047 / 100;

    soundInit();
    soundPowerOn();
    play();
}

void music_toggle(void) {
    if (!g_data) return;
    if (g_playing) {
        soundStop(1U << MUSIC_CH);
        g_playing = false;
    } else {
        play();                    /* starts over, like the DSi Shop */
    }
}

void music_update_lid(bool closed) {
    if (!g_data || closed == g_lid_closed) return;
    g_lid_closed = closed;
    if (closed) {
        g_resume_after_lid = g_playing;
        if (g_playing) soundStop(1U << MUSIC_CH);
        g_playing = false;
    } else {
        if (g_resume_after_lid) play();
        g_resume_after_lid = false;
    }
}

void music_stop(void) {
    if (g_playing) soundStop(1U << MUSIC_CH);
    g_playing = false;
}

void music_apply(const Config *config) {
    if (!g_data) {                 /* never loaded: it was off at startup */
        music_start(config);
        return;
    }
    g_vol = (unsigned)config->music_volume * 2047 / 100;
    if (!config->music)   music_stop();
    else if (!g_playing)  play();
    else                  soundChSetVolume(MUSIC_CH, g_vol);
}
