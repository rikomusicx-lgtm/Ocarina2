// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project

#include "yuzu/ocarina2/sound.h"

#include "common/common_types.h"

#include <cmath>
#include <cstring>
#include <vector>

#include <QApplication>
#include <QThread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Ocarina2Ui {
namespace {

constexpr int kSampleRate = 44100;
constexpr int kChannels = 1;
constexpr int kBytesPerSample = 2;  // 16-bit signed PCM

struct Tone {
    double frequency;
    double duration;  // seconds
    double gain;
    double attack;    // seconds
    double release;   // seconds
    double detune = 1.0;
};

struct EffectSpec {
    std::vector<Tone> tones;
    double duration;
};

EffectSpec MakeEffect(SoundPlayer::Effect effect) {
    switch (effect) {
    case SoundPlayer::Effect::Navigate:
        // A quiet woodblock tick.
        return {{Tone{2093.0, 0.055, 0.35, 0.002, 0.040, 1.0}, Tone{4186.0, 0.040, 0.10, 0.001, 0.030, 1.0}}, 0.075};
    case SoundPlayer::Effect::Confirm:
        // A short, bright "ding".
        return {{Tone{1318.5, 0.14, 0.42, 0.004, 0.115, 1.0}, Tone{2637.0, 0.10, 0.18, 0.002, 0.085, 1.0},
                 Tone{523.25, 0.16, 0.13, 0.002, 0.130, 1.0}}, 0.17};
    case SoundPlayer::Effect::Back:
        // A low, soft "blip".
        return {{Tone{659.25, 0.095, 0.38, 0.003, 0.075, 1.0}}, 0.115};
    case SoundPlayer::Effect::Launch:
        // Rising two-chime arpeggio.
        return {{Tone{523.25, 0.10, 0.38, 0.002, 0.075, 1.0}, Tone{783.99, 0.10, 0.38, 0.002, 0.075, 1.0},
                 Tone{1046.5, 0.15, 0.48, 0.002, 0.125, 1.0}, Tone{1568.0, 0.09, 0.22, 0.002, 0.080, 1.0}},
                0.34};
    case SoundPlayer::Effect::TabSwitch:
        // A dry tock for tabs.
        return {{Tone{1567.98, 0.045, 0.32, 0.001, 0.035, 1.0}}, 0.060};
    case SoundPlayer::Effect::Error:
        // A duller, lower buzz for refused actions.
        return {{Tone{196.0, 0.21, 0.40, 0.004, 0.170, 1.0}, Tone{185.0, 0.21, 0.27, 0.004, 0.170, 1.0}}, 0.26};
    }
    return {{}, 0.0};
}

/// A 16-bit signed PCM tone burst with a soft attack and exponential release, optionally with a
/// quiet octave partial for a chime-like quality.
void AppendTone(std::vector<int16_t>& pcm, const Tone& tone, int offset) {
    const int total = static_cast<int>(tone.duration * kSampleRate);
    const double two_pi_f = 2.0 * M_PI * tone.frequency;
    const double two_pi_d = 2.0 * M_PI * tone.frequency * tone.detune;
    for (int i = 0; i < total; ++i) {
        const double t = static_cast<double>(i) / kSampleRate;
        double envelope = 1.0;
        if (t < tone.attack) {
            envelope = t / tone.attack;
        } else if (t > tone.duration - tone.release) {
            const double remaining = (tone.duration - t) / tone.release;
            envelope = std::pow(std::max(0.0, remaining), 1.6);
        }
        const double fundamental = std::sin(two_pi_f * t);
        const double partial = 0.24 * std::sin(two_pi_d * t * 2.0);
        const double sample =
            (fundamental * 0.76 + partial) * envelope * tone.gain * 32767.0;
        const int at = offset + i;
        if (at >= 0 && at < static_cast<int>(pcm.size())) {
            pcm[static_cast<size_t>(at)] = static_cast<int16_t>(std::clamp(sample, -32768.0, 32767.0));
        }
    }
}

/// A complete RIFF/WAVE buffer for the effect, ready for PlaySound(SND_MEMORY).
QByteArray RenderWav(SoundPlayer::Effect effect) {
    const EffectSpec spec = MakeEffect(effect);
    const int total_samples = static_cast<int>(spec.duration * kSampleRate) + 64;
    std::vector<int16_t> pcm(static_cast<size_t>(total_samples), 0);
    int offset = 0;
    for (const Tone& tone : spec.tones) {
        AppendTone(pcm, tone, offset);
        offset += static_cast<int>(tone.duration * kSampleRate);
    }
    const u32 data_bytes = static_cast<u32>(pcm.size() * sizeof(int16_t));
    const u32 riff_size = 36 + data_bytes;

    QByteArray out;
    out.reserve(44 + static_cast<int>(data_bytes));
    auto write_u32 = [&out](u32 v) { out.append(reinterpret_cast<const char*>(&v), 4); };
    auto write_u16 = [&out](u16 v) { out.append(reinterpret_cast<const char*>(&v), 2); };
    out.append("RIFF", 4);
    write_u32(riff_size);
    out.append("WAVE", 4);
    out.append("fmt ", 4);
    write_u32(16);                 // PCM header size
    write_u16(1);                  // audio format: PCM
    write_u16(kChannels);
    write_u32(kSampleRate);
    write_u32(kSampleRate * kChannels * kBytesPerSample);  // byte rate
    write_u16(kChannels * kBytesPerSample);                // block align
    write_u16(8 * kBytesPerSample);                        // bits per sample
    out.append("data", 4);
    write_u32(data_bytes);
    out.append(reinterpret_cast<const char*>(pcm.data()), static_cast<int>(pcm.size() * sizeof(int16_t)));
    return out;
}

} // namespace

SoundPlayer& SoundPlayer::Instance() {
    static SoundPlayer player;
    return player;
}

SoundPlayer::SoundPlayer() = default;

void SoundPlayer::Play(Effect effect) {
    const QByteArray wav = RenderWav(effect);
#ifdef _WIN32
    if (!wav.isEmpty()) {
        // Fires asynchronously; the buffer is copied by the sound system, so it can be freed
        // with the local.
        if (PlaySoundA(reinterpret_cast<LPCSTR>(wav.constData()), nullptr,
                       SND_MEMORY | SND_ASYNC | SND_NODEFAULT)) {
            return;
        }
    }
    QApplication::beep();
#else
    // Other platforms: sequential beeps, enough to tell the calls apart.
    int repeats = 1;
    switch (effect) {
    case Effect::Navigate: repeats = 1; break;
    case Effect::Confirm: repeats = 2; break;
    case Effect::Back: repeats = 1; break;
    case Effect::Launch: repeats = 3; break;
    case Effect::TabSwitch: repeats = 1; break;
    case Effect::Error: repeats = 2; break;
    }
    for (int i = 0; i < repeats; ++i) {
        QApplication::beep();
        if (repeats > 1 && i + 1 < repeats) {
            QThread::msleep(70);
        }
    }
#endif
}

} // namespace Ocarina2Ui
