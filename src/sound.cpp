#include "sound.h"
#include "settings.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// All sounds use the non-blocking MessageBeep or async Beep via thread
// to avoid blocking the render loop. We use a fire-and-forget thread.
#include <thread>

static void beep_async(DWORD freq, DWORD ms) {
    std::thread([freq, ms]() { Beep(freq, ms); }).detach();
}

void sound_play(SoundEvent ev) {
    if (!g_settings.sounds_enabled) return;
    switch (ev) {
    case SoundEvent::KEY_TYPE:
        // Very short high tick — subtle keyboard feel
        beep_async(1200, 12);
        break;
    case SoundEvent::BACKSPACE:
        beep_async(800, 10);
        break;
    case SoundEvent::CMD_SEND:
        // Two-tone confirm
        beep_async(900, 40);
        break;
    case SoundEvent::CMD_ERROR:
        // Low buzz
        beep_async(200, 120);
        break;
    case SoundEvent::CMD_CLEAR:
        beep_async(1000, 30);
        break;
    case SoundEvent::CMD_STARS:
    case SoundEvent::CMD_LOGOUT:
        // Descending tone
        beep_async(1400, 60);
        break;
    case SoundEvent::AUTH_OK:
        // Ascending chord
        beep_async(600, 80);
        break;
    case SoundEvent::AUTH_FAIL:
        beep_async(300, 200);
        break;
    case SoundEvent::PLAYER_JOIN:
        beep_async(1100, 50);
        break;
    case SoundEvent::PLAYER_LEAVE:
        beep_async(500, 80);
        break;
    }
}

#else
// On Linux/Mac — no sound for now (can add SDL_mixer or similar later)
void sound_play(SoundEvent) {}
#endif
