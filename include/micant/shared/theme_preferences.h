#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Fixed-size little-endian record for per-user theme preferences. */
#define MICANT_THEME_PREFERENCES_MAGIC 0x5054484Du /* MTHP */
#define MICANT_THEME_PREFERENCES_VERSION 1u
#define MICANT_THEME_PREFERENCES_SIZE 32u

typedef enum MicaThemePreferenceThemeId {
    MICANT_THEME_PREFERENCE_THEME_WHITESUR = 1u,
} MicaThemePreferenceThemeId;

typedef enum MicaThemePreferenceMode {
    MICANT_THEME_PREFERENCE_MODE_LIGHT = 0u,
    MICANT_THEME_PREFERENCE_MODE_DARK = 1u,
    MICANT_THEME_PREFERENCE_MODE_AUTO = 2u,
} MicaThemePreferenceMode;

typedef enum MicaThemePreferenceAccentId {
    MICANT_THEME_PREFERENCE_ACCENT_DEFAULT = 0u,
    MICANT_THEME_PREFERENCE_ACCENT_PURPLE = 1u,
} MicaThemePreferenceAccentId;

enum MicaThemePreferenceEffects {
    MICANT_THEME_PREFERENCE_EFFECT_TRANSPARENCY = 1u << 0,
    MICANT_THEME_PREFERENCE_EFFECT_ROUNDED_CORNERS = 1u << 1,
    MICANT_THEME_PREFERENCE_EFFECT_SHADOWS = 1u << 2,
    MICANT_THEME_PREFERENCE_EFFECT_BLUR = 1u << 3,
    MICANT_THEME_PREFERENCE_EFFECT_REDUCED_MOTION = 1u << 4,
};
#define MICANT_THEME_PREFERENCE_KNOWN_EFFECTS 0x1Fu

typedef struct MicaThemePreferences {
    uint32_t magic;
    uint16_t version;
    uint16_t byteSize;
    uint32_t themeId;
    uint32_t mode;
    uint32_t accentId;
    uint16_t dayStartMinute;
    uint16_t nightStartMinute;
    uint32_t effects;
    uint32_t reserved;
} MicaThemePreferences;

#if defined(__cplusplus)
static_assert(sizeof(MicaThemePreferences) == MICANT_THEME_PREFERENCES_SIZE);
static_assert(offsetof(MicaThemePreferences, themeId) == 8);
static_assert(offsetof(MicaThemePreferences, effects) == 24);
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(MicaThemePreferences) == MICANT_THEME_PREFERENCES_SIZE,
               "MicaThemePreferences ABI size");
_Static_assert(offsetof(MicaThemePreferences, themeId) == 8,
               "MicaThemePreferences ABI theme offset");
_Static_assert(offsetof(MicaThemePreferences, effects) == 24,
               "MicaThemePreferences ABI effects offset");
#endif

#ifdef __cplusplus
inline void MicaThemePreferencesInitializeDefaults(
    MicaThemePreferences* preferences) noexcept {
    if (!preferences) return;
    *preferences = MicaThemePreferences{
        MICANT_THEME_PREFERENCES_MAGIC,
        MICANT_THEME_PREFERENCES_VERSION,
        MICANT_THEME_PREFERENCES_SIZE,
        MICANT_THEME_PREFERENCE_THEME_WHITESUR,
        MICANT_THEME_PREFERENCE_MODE_LIGHT,
        MICANT_THEME_PREFERENCE_ACCENT_DEFAULT,
        6u * 60u,
        18u * 60u,
        0u,
        0u,
    };
}

/* Parse only the defined v1 record. Unknown versions and trailing/truncated
   bytes fail so callers can retain their current valid preferences. */
inline bool MicaThemePreferencesParse(const void* bytes, size_t byteLength,
                                      MicaThemePreferences* output) noexcept {
    if (!bytes || !output || byteLength != sizeof(MicaThemePreferences)) return false;
    MicaThemePreferences candidate{};
    memcpy(&candidate, bytes, sizeof(candidate));
    if (candidate.magic != MICANT_THEME_PREFERENCES_MAGIC ||
        candidate.version != MICANT_THEME_PREFERENCES_VERSION ||
        candidate.byteSize != sizeof(MicaThemePreferences) ||
        candidate.themeId != MICANT_THEME_PREFERENCE_THEME_WHITESUR ||
        candidate.mode > MICANT_THEME_PREFERENCE_MODE_AUTO ||
        candidate.accentId > MICANT_THEME_PREFERENCE_ACCENT_PURPLE ||
        candidate.dayStartMinute >= 24u * 60u ||
        candidate.nightStartMinute >= 24u * 60u ||
        candidate.dayStartMinute == candidate.nightStartMinute ||
        (candidate.effects & ~uint32_t{MICANT_THEME_PREFERENCE_KNOWN_EFFECTS}) != 0 ||
        candidate.reserved != 0)
        return false;
    *output = candidate;
    return true;
}

inline bool MicaThemePreferencesIsValid(
    const MicaThemePreferences* preferences) noexcept {
    MicaThemePreferences parsed{};
    return preferences && MicaThemePreferencesParse(
        preferences, sizeof(*preferences), &parsed);
}
#endif
