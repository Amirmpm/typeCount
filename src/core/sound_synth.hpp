#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace tc {
namespace sound {

// Synthesize ultra-soft, velvety, warm "pastel" acoustic notification tones
// at 44100 Hz 16-bit PCM mono WAV in memory (zero external audio asset files).
inline std::vector<uint8_t> synthesize_pastel_wav(const std::string& preset) {
    if (preset == "none") return {};

    const uint32_t rate = 44100;
    const double pi = 3.14159265358979323846;
    const double pi2 = 2.0 * pi;

    auto soft_attack = [&](double dt, double attack_sec) -> double {
        if (dt <= 0.0) return 0.0;
        if (dt >= attack_sec) return 1.0;
        return 0.5 * (1.0 - std::cos(pi * dt / attack_sec));
    };

    auto fade_out_tail = [&](double t, double total_sec, double fade_sec) -> double {
        double rem = total_sec - t;
        if (rem <= 0.0) return 0.0;
        if (rem >= fade_sec) return 1.0;
        return 0.5 * (1.0 - std::cos(pi * rem / fade_sec));
    };

    struct PartialNote {
        double start_s;
        double freq_hz;
        double amp;
        double decay;
        double attack_s;
        double warm2;   // subtle 2x octave warmth
        double warm15;  // subtle 1.5x velvet harmonic
    };

    double duration_s = 0.95;
    std::vector<PartialNote> notes;
    bool dew_glide = false;

    if (preset == "mist_chime") {
        // Morning Mist: delicate C5 -> E5 -> G5 airy shimmer
        duration_s = 1.05;
        notes = {
            {0.00, 523.25, 0.14, 4.2, 0.038, 0.10, 0.03},
            {0.17, 659.25, 0.15, 3.9, 0.040, 0.11, 0.03},
            {0.34, 783.99, 0.14, 3.2, 0.045, 0.09, 0.02},
        };
    } else if (preset == "peach_marimba") {
        // Peach Marimba: warm felt-mallet wooden D4 -> A4
        duration_s = 0.85;
        notes = {
            {0.00, 293.66, 0.19, 6.2, 0.022, 0.14, 0.05},
            {0.15, 440.00, 0.17, 5.4, 0.024, 0.12, 0.04},
        };
    } else if (preset == "lavender_harp") {
        // Lavender Harp: soft cascading Gmaj7 arpeggio
        duration_s = 1.15;
        notes = {
            {0.00, 392.00, 0.12, 3.6, 0.032, 0.12, 0.04},
            {0.12, 493.88, 0.12, 3.5, 0.034, 0.10, 0.04},
            {0.24, 587.33, 0.12, 3.4, 0.036, 0.09, 0.03},
            {0.36, 739.99, 0.11, 2.9, 0.042, 0.07, 0.02},
        };
    } else if (preset == "mint_dew") {
        // Mint Dewdrop: ultra-gentle harmonic water droplet + soft halo
        duration_s = 0.80;
        dew_glide = true;
        notes = {
            {0.16, 523.25, 0.13, 4.4, 0.045, 0.06, 0.02},
        };
    } else if (preset == "silk_breeze") {
        // Silk Breeze: slow soothing warm low chord swell (C4 + G4 + E5)
        duration_s = 1.20;
        notes = {
            {0.00, 261.63, 0.12, 2.2, 0.140, 0.08, 0.03},
            {0.04, 392.00, 0.11, 2.3, 0.150, 0.07, 0.03},
            {0.10, 659.25, 0.09, 2.5, 0.170, 0.05, 0.02},
        };
    } else {
        // Default: Pastel Pearl (Fmaj7 warm velvet bloom: F4 -> A4 -> C5)
        duration_s = 1.00;
        notes = {
            {0.00, 349.23, 0.14, 3.8, 0.036, 0.10, 0.04},
            {0.15, 440.00, 0.15, 3.6, 0.038, 0.10, 0.04},
            {0.30, 523.25, 0.14, 3.1, 0.042, 0.08, 0.03},
        };
    }

    const uint32_t num_samples = static_cast<uint32_t>(std::round(duration_s * rate));
    const uint32_t data_bytes = num_samples * 2;
    const uint32_t file_size = 44 + data_bytes;
    std::vector<uint8_t> wav(file_size, 0);

    auto put32 = [&](size_t off, uint32_t v) {
        wav[off] = v & 0xFF;
        wav[off + 1] = (v >> 8) & 0xFF;
        wav[off + 2] = (v >> 16) & 0xFF;
        wav[off + 3] = (v >> 24) & 0xFF;
    };
    auto put16 = [&](size_t off, uint16_t v) {
        wav[off] = v & 0xFF;
        wav[off + 1] = (v >> 8) & 0xFF;
    };

    std::memcpy(&wav[0], "RIFF", 4);
    put32(4, file_size - 8);
    std::memcpy(&wav[8], "WAVE", 4);
    std::memcpy(&wav[12], "fmt ", 4);
    put32(16, 16);
    put16(20, 1);         // PCM
    put16(22, 1);         // Mono
    put32(24, rate);
    put32(28, rate * 2);
    put16(32, 2);
    put16(34, 16);
    std::memcpy(&wav[36], "data", 4);
    put32(40, data_bytes);

    double dew_phase = 0.0;
    for (uint32_t i = 0; i < num_samples; ++i) {
        double t = static_cast<double>(i) / rate;
        double mix = 0.0;

        if (dew_glide) {
            double env = soft_attack(t, 0.018) * std::exp(-t * 10.5);
            double f_inst = 460.0 + 180.0 * (1.0 - std::exp(-t * 16.0));
            dew_phase += pi2 * f_inst / rate;
            mix += std::sin(dew_phase) * env * 0.17;
        }

        for (const auto& n : notes) {
            if (t < n.start_s) continue;
            double dt = t - n.start_s;
            double env = soft_attack(dt, n.attack_s) * std::exp(-dt * n.decay);
            double osc = std::sin(pi2 * n.freq_hz * dt)
                       + n.warm2 * std::sin(pi2 * n.freq_hz * 2.0 * dt) * std::exp(-dt * (n.decay * 1.6))
                       + n.warm15 * std::sin(pi2 * n.freq_hz * 1.5 * dt) * std::exp(-dt * (n.decay * 1.8));
            mix += osc * env * n.amp;
        }

        mix *= fade_out_tail(t, duration_s, 0.14);
        mix = std::max(-0.24, std::min(0.24, mix)); // Soft ceiling guarantees gentle, non-harsh playback
        int16_t pcm = static_cast<int16_t>(std::round(mix * 32767.0));
        put16(44 + i * 2, static_cast<uint16_t>(pcm));
    }
    return wav;
}

inline const std::vector<uint8_t>& get_cached_pastel_wav(const std::string& preset) {
    static std::mutex mu;
    static std::unordered_map<std::string, std::vector<uint8_t>> cache;
    std::string key = preset.empty() ? "pastel_pearl" : preset;
    std::lock_guard<std::mutex> lk(mu);
    auto it = cache.find(key);
    if (it == cache.end()) {
        it = cache.emplace(key, synthesize_pastel_wav(key)).first;
    }
    return it->second;
}

} // namespace sound
} // namespace tc
