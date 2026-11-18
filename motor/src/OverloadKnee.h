// Maestruc — cópia de RASGO/REFERENCIAS/SAIDA_TECNICA/src/OverloadKnee.h (saida-ref 1.0,
// 16/11/2026, sha256 601c8c2530d4c719…), namespace maestruc. Proveniência completa abaixo.
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Lúcio de Araújo

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace maestruc
{
// OPTIONAL stage, before OutputStage, for buses that sum many sources.
//
// Provenance: copied on 2026-11-16 from the Studio
// (PROJETOS_AUTORAIS/RASGO_SYNTH_STUDIO, commit 3fc8ffd,
// rasgo-synth-core/src/dsp/LinkedOverloadProtector.hpp, sha256 59b0879a...,
// AGPL-3.0-or-later, same author). Adapted: the rasgo::saida namespace and
// noexcept; behaviour unchanged.
//
// A matched stereo overload knee for the summed creative bus. Below the
// threshold this is mathematically an identity operation. Above it, both
// channels use the same continuous transfer curve. This avoids the abrupt
// gain changes a peak-derived linked multiplier can create when the louder
// channel alternates, while preventing dozens of individually-safe sources
// from forcing the final true-peak limiter into tens of decibels of reduction.
// It is overload protection, not loudness normalization or an always-on tone.
class OverloadKnee final
{
public:
    void setThresholdAndLimit(float threshold, float limit) noexcept
    {
        threshold_ = std::clamp(threshold, 0.1f, 1.0f);
        limit_ = std::clamp(limit, threshold_ + 0.01f, 2.0f);
    }

    void process(float inL, float inR, float& outL, float& outR) noexcept
    {
        if (!std::isfinite(inL)) inL = 0.0f;
        if (!std::isfinite(inR)) inR = 0.0f;
        const float peak = std::max(std::fabs(inL), std::fabs(inR));
        if (peak > threshold_)
        {
            ++overloadFrames_;
            const float protectedPeak = protectMagnitude(peak);
            const float reduction = 20.0f * std::log10(peak / std::max(protectedPeak, 1.0e-9f));
            maxReductionDb_ = std::max(maxReductionDb_, reduction);
        }
        outL = protectSample(inL);
        outR = protectSample(inR);
    }

    [[nodiscard]] std::size_t overloadFrames() const noexcept { return overloadFrames_; }
    [[nodiscard]] float maxReductionDb() const noexcept { return maxReductionDb_; }
    void clearMeters() noexcept { overloadFrames_ = 0; maxReductionDb_ = 0.0f; }

private:
    float protectMagnitude(float magnitude) const noexcept
    {
        if (magnitude <= threshold_) return magnitude;
        const float width = limit_ - threshold_;
        return threshold_ + width * std::tanh((magnitude - threshold_) / width);
    }

    float protectSample(float sample) const noexcept
    {
        return std::copysign(protectMagnitude(std::fabs(sample)), sample);
    }

    float threshold_ = 0.78f;
    float limit_ = 1.18f;
    std::size_t overloadFrames_ = 0;
    float maxReductionDb_ = 0.0f;
};
}
