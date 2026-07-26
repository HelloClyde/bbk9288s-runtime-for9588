#ifndef BBK9288S_COMPAT_AUDIO_H
#define BBK9288S_COMPAT_AUDIO_H

#include <stdint.h>

#include "c33vm.h"

enum compat_audio_slot {
    COMPAT_AUDIO_OPEN = 0,
    COMPAT_AUDIO_CLOSE = 1,
    COMPAT_AUDIO_CLOSE_ALL = 2,
    COMPAT_AUDIO_SEEK = 3,
    COMPAT_AUDIO_PLAY = 4,
    COMPAT_AUDIO_RECORD = 5,
    COMPAT_AUDIO_PAUSE = 6,
    COMPAT_AUDIO_RESUME = 7,
    COMPAT_AUDIO_STOP = 8,
    COMPAT_AUDIO_READ = 9,
    COMPAT_AUDIO_WRITE = 10,
    COMPAT_AUDIO_GET_STATUS = 11,
    COMPAT_AUDIO_GET_MEDIA_INFO = 12,
    COMPAT_AUDIO_SET_CALLBACK = 13,
    COMPAT_AUDIO_GET_LYRIC = 14,
    COMPAT_AUDIO_MUTE = 15,
    COMPAT_AUDIO_SET_VOLUME = 16,
    COMPAT_AUDIO_GET_VOLUME = 17,
    COMPAT_AUDIO_SET_EQ = 18,
    COMPAT_AUDIO_GET_EQ = 19,
    COMPAT_AUDIO_MIXER_SOURCE_OPEN = 20,
    COMPAT_AUDIO_MIXER_SOURCE_CLOSE = 21,
    COMPAT_AUDIO_MIXER_OPEN = 22,
    COMPAT_AUDIO_MIXER_SET_CHANNEL = 23,
    COMPAT_AUDIO_MIXER_START = 24,
    COMPAT_AUDIO_MIXER_STOP = 25,
    COMPAT_AUDIO_MIXER_CLOSE = 26,
    COMPAT_AUDIO_GET_FAV_PARAM = 27,
    COMPAT_AUDIO_SET_CHANNEL = 28,
    COMPAT_AUDIO_GET_CHANNEL = 29
};

/*
 * Parse the PCM payload of a 9288S MIXERSOURCE in guest memory. RIFF/WAVE
 * sources are changed in place from the complete file to the data chunk;
 * already-decoded raw sources remain unchanged.
 */
int compat_audio_mixer_source_open(
    c33_vm_t *vm,
    uint32_t source_address
);

#endif
