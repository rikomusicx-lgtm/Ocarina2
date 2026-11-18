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
};

struct EffectSpec {
    std::vector<Tone> tones;
    double duration;
};

EffectSpec MakeEffect(SoundPlayer::Effect effect) {
    switch (effect) {
    case SoundPlayer::Effect::Navigate:
        // A barely audible tick - at scroll speed it must not rattle (or sound like a fault).
        return {{Tone{523.25, 0.038, 0.10, 0.002, 0.030}}, 0.045};
    case SoundPlayer::Effect::Confirm:
        // A warm, clean confirmation - no chirp, no sweep.
        return {{Tone{659.25, 0.110, 0.45, 0.004, 0.075}}, 0.120};
    case SoundPlayer::Effect::Back:
        // Lower and gentler - backing out.
        return {{Tone{392.00, 0.095, 0.42, 0.004, 0.060}}, 0.105};
    case SoundPlayer::Effect::Launch:
        // Two soft rising notes - start playing.
        return {{Tone{523.25, 0.075, 0.45, 0.004, 0.045}, Tone{784.00, 0.115, 0.45, 0.004, 0.085}},
                0.205};
    case SoundPlayer::Effect::TabSwitch:
        // Short and quiet - moving between tabs.
        return {{Tone{587.33, 0.055, 0.34, 0.003, 0.035}}, 0.065};
    case SoundPlayer::Effect::Error:
        // A low, dull refusal.
        return {{Tone{220.00, 0.140, 0.40, 0.004, 0.100}, Tone{233.08, 0.140, 0.26, 0.004, 0.100}},
                0.155};
    }
    return {{}, 0.0};
}

/// A 16-bit signed PCM tone with raised-cosine fades at both ends, so every note starts and
/// ends at exactly zero with zero slope - no clicks, no pops, no glitches.
void AppendTone(std::vector<int16_t>& pcm, const Tone& tone, int offset) {
    const int total = static_cast<int>(tone.duration * kSampleRate);
    const double two_pi_f = 2.0 * M_PI * tone.frequency;
    for (int i = 0; i < total; ++i) {
        const double t = static_cast<double>(i) / kSampleRate;
        double envelope = 1.0;
        if (t < tone.attack) {
            envelope = 0.5 - 0.5 * std::cos(M_PI * t / tone.attack);
        } else if (t > tone.duration - tone.release) {
            const double remaining = (tone.duration - t) / tone.release;
            envelope = 0.5 + 0.5 * std::cos(M_PI * (1.0 - remaining));
        }
        const double phase = two_pi_f * t;
        const double sample =
            (std::sin(phase) * 0.88 + std::sin(phase * 2.0) * 0.07) * envelope * tone.gain *
            32767.0;
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
        DWORD flags = SND_MEMORY | SND_ASYNC | SND_NODEFAULT;
        // The scroll tick re-triggers ~12 times per second; without SND_NOSTOP each tick
        // cuts the previous one off mid-wave and the scroll sounds like a stutter.
        const bool nostop = effect == Effect::Navigate || effect == Effect::TabSwitch;
        if (nostop) {
            flags |= SND_NOSTOP;
        }
        if (PlaySoundA(reinterpret_cast<LPCSTR>(wav.constData()), nullptr, flags)) {
            return;
        }
        // With SND_NOSTOP, a tick that is suppressed because the previous one is still
        // playing comes back as a failure - that is intentional, not an error, and must
        // not turn into the Windows ding.
        if (nostop) {
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
