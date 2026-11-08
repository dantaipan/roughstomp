// Regression renders: every pedal, at its defaults and pushed hard, runs a
// fixed synthetic guitar part. The result's fingerprint (energy in five bands,
// per 2048-sample block, per channel) must match the reference file in
// tests/regression to within 0.1 dB, so any audible change to a pedal fails
// here and says where.
//
// After an intended change, regenerate the references and review the diff:
//   RS_UPDATE_REGRESSION=1 build/release/fx-core/fx-core-tests "[regression]"

#include <catch2/catch_test_macros.hpp>

#include "fx/Filters.h"
#include "fx/Pedal.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace
{
constexpr double fs = 48000.0;
constexpr int blockSize = 2048;
constexpr int processBlock = 256;
constexpr double toleranceDb = 0.1;
constexpr double floorDb = -90.0;   // quieter than this counts as silence

/** Plucked strings (Karplus-Strong): E2 A2 D3 G3 B3 E4, a quarter second
    apart, getting louder, then half a second for tails. Deterministic. */
std::vector<float> guitarPart()
{
    const double notes[] { 82.41, 110.0, 146.83, 196.0, 246.94, 329.63 };
    const int spacing = static_cast<int> (0.25 * fs);
    std::vector<float> out (static_cast<size_t> (6 * spacing + static_cast<int> (0.5 * fs)), 0.0f);

    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> excite (-1.0f, 1.0f);
    for (int n = 0; n < 6; ++n)
    {
        const auto period = static_cast<size_t> (std::lround (fs / notes[n]));
        std::vector<float> line (period);
        for (auto& s : line)
            s = excite (rng);

        const float amplitude = 0.08f + 0.06f * static_cast<float> (n);
        size_t pos = 0;
        for (size_t i = static_cast<size_t> (n * spacing); i < out.size(); ++i)
        {
            const size_t next = (pos + 1) % period;
            const float v = line[pos];
            line[pos] = 0.996f * 0.5f * (line[pos] + line[next]);
            out[i] += amplitude * v;
            pos = next;
        }
    }
    return out;
}

/** Energy in dB per band (lows, low mids, high mids, presence, air) for each
    block of each channel, as text lines. */
std::vector<std::string> fingerprint (const std::vector<std::vector<float>>& channels)
{
    std::vector<std::string> lines;
    for (size_t c = 0; c < channels.size(); ++c)
    {
        const auto& x = channels[c];
        std::array<fx::Biquad, 5> bands;
        bands[0].setCoefficients (fx::BiquadCoeffs::lowPass (fs, 200.0));
        bands[1].setCoefficients (fx::BiquadCoeffs::bandPass (fs, 400.0, 0.7));
        bands[2].setCoefficients (fx::BiquadCoeffs::bandPass (fs, 1600.0, 0.7));
        bands[3].setCoefficients (fx::BiquadCoeffs::bandPass (fs, 5000.0, 0.7));
        bands[4].setCoefficients (fx::BiquadCoeffs::highPass (fs, 9000.0));

        for (size_t start = 0; start + blockSize <= x.size(); start += blockSize)
        {
            std::ostringstream line;
            line << "ch" << c << " block" << start / blockSize;
            for (auto& band : bands)
            {
                double sum = 0.0;
                for (size_t i = start; i < start + blockSize; ++i)
                {
                    const double v = band.processSample (x[i]);
                    sum += v * v;
                }
                const double dB = std::max (floorDb, 10.0 * std::log10 (sum / blockSize + 1e-30));
                line << " " << std::fixed;
                line.precision (2);
                line << dB;
            }
            lines.push_back (line.str());
        }
    }
    return lines;
}

std::vector<double> numbersIn (const std::string& line)
{
    std::istringstream in (line);
    std::string word;
    std::vector<double> values;
    in >> word >> word;   // "chN blockM"
    double v;
    while (in >> v)
        values.push_back (v);
    return values;
}

struct Case
{
    std::string name;
    std::vector<std::pair<std::string, float>> normalised;   // param id -> 0..1 (empty: defaults)
};

/** Defaults, and every continuous control at 80% of its travel with
    switches on their last choice. */
std::vector<Case> casesFor (const fx::PedalInfo& info)
{
    Case pushed { "pushed", {} };
    for (const auto& p : info.params)
        pushed.normalised.emplace_back (std::string (p.id), p.kind == fx::ParamInfo::Kind::continuous ? 0.8f : 1.0f);
    return { { "default", {} }, pushed };
}

std::vector<std::vector<float>> render (const fx::PedalInfo& info, const Case& c, int numChannels)
{
    auto pedal = fx::registry::create (info.id);
    pedal->prepare (fs, processBlock, numChannels);
    pedal->setTempo (120.0);
    for (const auto& [id, n] : c.normalised)
    {
        const int i = info.indexOf (id);
        pedal->setParameter (i, info.params[static_cast<size_t> (i)].fromNormalised (n));
    }

    const auto input = guitarPart();
    std::vector<std::vector<float>> out (static_cast<size_t> (numChannels), input);
    for (size_t pos = 0; pos < input.size(); pos += processBlock)
    {
        std::vector<float*> ptrs;
        for (auto& ch : out)
            ptrs.push_back (ch.data() + pos);
        pedal->process (ptrs.data(), numChannels, static_cast<int> (std::min<size_t> (processBlock, input.size() - pos)));
    }
    return out;
}
} // namespace

TEST_CASE ("Regression renders match their references", "[regression]")
{
    const std::filesystem::path dir = FX_REGRESSION_DIR;
    const bool update = std::getenv ("RS_UPDATE_REGRESSION") != nullptr;

    for (const auto* info : fx::registry::all())
        for (const auto& c : casesFor (*info))
        {
            const std::string name = std::string (info->id) + "-" + c.name;
            CAPTURE (name);

            const auto lines = fingerprint (render (*info, c, 2));
            const auto file = dir / (name + ".txt");

            if (update)
            {
                std::filesystem::create_directories (dir);
                std::ofstream out (file);
                for (const auto& l : lines)
                    out << l << "\n";
                continue;
            }

            std::ifstream in (file);
            if (! in.good())
            {
                FAIL_CHECK ("No reference at " << file.string() << ": run with RS_UPDATE_REGRESSION=1 to create it");
                continue;
            }

            std::vector<std::string> expected;
            for (std::string l; std::getline (in, l);)
                if (! l.empty())
                    expected.push_back (l);
            REQUIRE (expected.size() == lines.size());

            // Report the first place it differs
            for (size_t i = 0; i < lines.size(); ++i)
            {
                const auto want = numbersIn (expected[i]), got = numbersIn (lines[i]);
                REQUIRE (want.size() == got.size());
                for (size_t b = 0; b < want.size(); ++b)
                    if (std::abs (want[b] - got[b]) > toleranceDb)
                    {
                        FAIL_CHECK (name << ": " << lines[i].substr (0, lines[i].find (" ", 4)) << " band " << b
                                         << " was " << want[b] << " dB, now " << got[b] << " dB");
                        goto nextCase;
                    }
            }
        nextCase:;
        }

    if (update)
        WARN ("Regression references rewritten in " << dir.string() << ": review the diff before committing");
}
