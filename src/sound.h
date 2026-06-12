#pragma once

enum class SoundEvent {
    KEY_TYPE,       // обычный ввод символа
    CMD_SEND,       // отправка команды (Enter)
    CMD_ERROR,      // неизвестная команда или ошибка
    CMD_CLEAR,      // команда clear
    CMD_STARS,      // команда stars / warp
    CMD_LOGOUT,     // команда logout
    AUTH_OK,        // успешный вход
    AUTH_FAIL,      // ошибка входа
    PLAYER_JOIN,    // другой игрок подключился
    PLAYER_LEAVE,   // другой игрок отключился
    BACKSPACE,      // удаление символа
};

void sound_play(SoundEvent ev);
