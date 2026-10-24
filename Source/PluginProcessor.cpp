#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

using APVTS = juce::AudioProcessorValueTreeState;

CalmaProcessor::CalmaProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (auto& d : displayReduction) d.store (0.0f);
    for (auto& d : displaySpec)      d.store (-120.0f);

    prDepth   = apvts.getRawParameterValue ("depth");
    prSens    = apvts.getRawParameterValue ("sens");
    prSharp   = apvts.getRawParameterValue ("sharp");
    prSelect  = apvts.getRawParameterValue ("select");
    prAttack  = apvts.getRawParameterValue ("attack");
    prRelease = apvts.getRawParameterValue ("release");
    prLow     = apvts.getRawParameterValue ("low");
    prHigh    = apvts.getRawParameterValue ("high");
    prMix     = apvts.getRawParameterValue ("mix");
    prTrim    = apvts.getRawParameterValue ("trim");
    prDelta   = apvts.getRawParameterValue ("delta");
    prMode    = apvts.getRawParameterValue ("mode");
    prProc    = apvts.getRawParameterValue ("proc");
    for (int i = 0; i < numEqNodes; ++i)
        prEq[(size_t) i] = apvts.getRawParameterValue ("eq" + juce::String (i));
}

APVTS::ParameterLayout CalmaProcessor::createLayout()
{
    using P    = juce::AudioParameterFloat;
    using ID   = juce::ParameterID;
    using Attr = juce::AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    auto pct = [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };
    auto ms  = [] (float v, int) { return juce::String (v, 1) + " ms"; };
    auto db  = [] (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; };
    auto hz  = [] (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz"
                                                       : juce::String (juce::roundToInt (v)) + " Hz"; };

    p.push_back (std::make_unique<P> (ID { "depth", 1 }, "Depth",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, Attr().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<P> (ID { "sens", 1 }, "Sensitivity",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, Attr().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<P> (ID { "sharp", 1 }, "Sharpness",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, Attr().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<P> (ID { "select", 1 }, "Selectivity",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.7f, Attr().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<P> (ID { "attack", 1 }, "Attack",
        juce::NormalisableRange<float> (1.0f, 200.0f, 0.1f, 0.4f), 10.0f, Attr().withStringFromValueFunction (ms)));
    p.push_back (std::make_unique<P> (ID { "release", 1 }, "Release",
        juce::NormalisableRange<float> (10.0f, 1000.0f, 0.1f, 0.4f), 120.0f, Attr().withStringFromValueFunction (ms)));
    p.push_back (std::make_unique<P> (ID { "low", 1 }, "Low Freq",
        juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.3f), 100.0f, Attr().withStringFromValueFunction (hz)));
    p.push_back (std::make_unique<P> (ID { "high", 1 }, "High Freq",
        juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.3f), 16000.0f, Attr().withStringFromValueFunction (hz)));
    p.push_back (std::make_unique<P> (ID { "mix", 1 }, "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f, Attr().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<P> (ID { "trim", 1 }, "Trim",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, Attr().withStringFromValueFunction (db)));
    p.push_back (std::make_unique<juce::AudioParameterBool> (ID { "delta", 1 }, "Delta", false));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (ID { "mode", 1 }, "Mode",
        juce::StringArray { "Soft", "Hard" }, 0));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (ID { "proc", 1 }, "Processing",
        juce::StringArray { "Stereo", "M/S", "Mid", "Side" }, 0));

    for (int i = 0; i < numEqNodes; ++i)
        p.push_back (std::make_unique<P> (ID { "eq" + juce::String (i), 1 },
                                          "Ref EQ " + juce::String (eqFreqs[(size_t) i], 0) + " Hz",
                                          juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
                                          Attr().withStringFromValueFunction (db)));

    return { p.begin(), p.end() };
}

bool CalmaProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == l.getMainInputChannelSet();
}

void CalmaProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    setLatencySamples (fftSize);

    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize);

    float sumSq = 0.0f;
    for (int j = 0; j < 4; ++j) { const float w = window[(size_t) (j * hopSize)]; sumSq += w * w; }
    olaNorm = 1.0f / sumSq;

    // tabela de interpolacao da EQ de referencia (log-frequencia, smoothstep)
    const float binHz = (float) fs / (float) fftSize;
    for (int k = 0; k < numBins; ++k)
    {
        const float f = juce::jmax ((float) k * binHz, 1.0f);
        int seg = 0;
        float t = 0.0f;
        if (f >= eqFreqs.back())
        {
            seg = numEqNodes - 2;
            t = 1.0f;
        }
        else if (f > eqFreqs[0])
        {
            while (seg < numEqNodes - 2 && f >= eqFreqs[(size_t) seg + 1]) ++seg;
            t = std::log (f / eqFreqs[(size_t) seg]) / std::log (eqFreqs[(size_t) seg + 1] / eqFreqs[(size_t) seg]);
            t = t * t * (3.0f - 2.0f * t);
        }
        eqSeg[(size_t) k] = seg;
        eqT[(size_t) k]   = t;
    }

    for (auto& a : inRing)  a.fill (0.0f);
    for (auto& a : outRing) a.fill (0.0f);
    for (auto& a : reduction) a.fill (0.0f);
    for (auto& a : smoothed)  a.fill (0.0f);
    pos = 0;
    hopCount = 0;
}

void CalmaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int nCh = juce::jmin (buffer.getNumChannels(), maxCh);
    const int n   = buffer.getNumSamples();

    pDepth  = prDepth->load();
    pSens   = prSens->load();
    pSharp  = prSharp->load();
    pSelect = prSelect->load();
    pMix    = prMix->load();
    pTrim   = juce::Decibels::decibelsToGain (prTrim->load());
    pLow    = prLow->load();
    pHigh   = prHigh->load();
    pDelta  = prDelta->load() > 0.5f;
    pMode   = juce::roundToInt (prMode->load());
    pProc   = juce::roundToInt (prProc->load());
    for (size_t i = 0; i < (size_t) numEqNodes; ++i) eqNode[i] = prEq[i]->load();

    const float attMs = prAttack->load();
    const float relMs = prRelease->load();
    attCoef = std::exp (-(float) hopSize / (float) (fs * attMs * 0.001));
    relCoef = std::exp (-(float) hopSize / (float) (fs * relMs * 0.001));

    float* ptr[maxCh] = { nullptr, nullptr };
    for (int ch = 0; ch < nCh; ++ch) ptr[ch] = buffer.getWritePointer (ch);

    for (int s = 0; s < n; ++s)
    {
        for (int ch = 0; ch < nCh; ++ch)
        {
            const float x   = ptr[ch][s];
            const float dry = inRing[(size_t) ch][(size_t) pos]; // amostra de ha fftSize amostras
            const float wet = outRing[(size_t) ch][(size_t) pos];
            outRing[(size_t) ch][(size_t) pos] = 0.0f;
            inRing[(size_t) ch][(size_t) pos]  = x;

            ptr[ch][s] = pDelta ? (dry - wet) : (dry + (wet - dry) * pMix) * pTrim;
        }

        pos = (pos + 1) & (fftSize - 1);

        if (++hopCount >= hopSize)
        {
            hopCount = 0;
            processFrame (nCh);
        }
    }
}

// detecta ressonancias e atualiza a reducao (dB) do estado "st"
void CalmaProcessor::updateReduction (int st, bool active)
{
    const auto& db = dbSpec[(size_t) st];
    auto& red = reduction[(size_t) st];

    prefix[0] = 0.0;
    for (int k = 0; k < numBins; ++k)
        prefix[(size_t) k + 1] = prefix[(size_t) k] + (double) db[(size_t) k];

    const bool  hard      = pMode == 1;
    const float factor    = pDepth * 1.5f;
    const float maxRed    = hard ? 24.0f : 15.0f;
    const float thrDb     = 12.0f * (1.0f - pSens);
    const float binHz     = (float) fs / (float) fftSize;
    const float widthFrac = 0.6f - 0.55f * pSelect; // selectivity alta = janela de referencia estreita

    for (int k = 0; k < numBins; ++k)
    {
        const int hw = juce::jmax (3, (int) ((float) k * widthFrac));
        const int lo = juce::jmax (0, k - hw);
        const int hi = juce::jmin (numBins - 1, k + hw);
        const float env = (float) ((prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / (double) (hi - lo + 1));

        const float f = (float) k * binHz;
        float target = 0.0f;
        if (active && f >= pLow && f <= pHigh)
        {
            // EQ de referencia: + = mais supressao naquela regiao
            const size_t seg = (size_t) eqSeg[(size_t) k];
            const float eqDb = eqNode[seg] * (1.0f - eqT[(size_t) k]) + eqNode[seg + 1] * eqT[(size_t) k];

            const float x = juce::jmax (0.0f, db[(size_t) k] - env - (thrDb - eqDb));
            const float shaped = hard ? x : (x * x) / (x + 6.0f); // soft = joelho suave
            target = juce::jmin (maxRed, factor * shaped);
        }

        float& r = red[(size_t) k];
        const float c = target > r ? attCoef : relCoef;
        r = target + c * (r - target);
    }

    // suavizacao em frequencia (kernel triangular); soft = mais larga, sharpness alta = mais estreita
    const int radius = hard ? 1 + juce::roundToInt ((1.0f - pSharp) * 4.0f)
                            : 2 + juce::roundToInt ((1.0f - pSharp) * 6.0f);
    auto& sm = smoothed[(size_t) st];
    for (int k = 0; k < numBins; ++k)
    {
        float sum = 0.0f, wsum = 0.0f;
        for (int j = -radius; j <= radius; ++j)
        {
            const int kk = juce::jlimit (0, numBins - 1, k + j);
            const float w = (float) (radius + 1 - std::abs (j));
            sum += w * red[(size_t) kk];
            wsum += w;
        }
        sm[(size_t) k] = sum / wsum;
    }
}

void CalmaProcessor::processFrame (int nCh)
{
    constexpr int mask = fftSize - 1;
    const bool ms = (nCh == 2 && pProc != 0); // 0 = Stereo (linkado), 1 = M/S, 2 = Mid, 3 = Side

    // monta os quadros (L/R ou M/S) com janela
    for (int i = 0; i < fftSize; ++i)
    {
        const size_t idx = (size_t) ((pos + i) & mask);
        const float w = window[(size_t) i];
        if (ms)
        {
            const float l = inRing[0][idx], r = inRing[1][idx];
            fftData[0][(size_t) i] = 0.5f * (l + r) * w;
            fftData[1][(size_t) i] = 0.5f * (l - r) * w;
        }
        else
        {
            for (int ch = 0; ch < nCh; ++ch)
                fftData[(size_t) ch][(size_t) i] = inRing[(size_t) ch][idx] * w;
        }
    }

    for (int ch = 0; ch < nCh; ++ch)
    {
        auto& d = fftData[(size_t) ch];
        std::fill (d.begin() + fftSize, d.end(), 0.0f);
        fft.performRealOnlyForwardTransform (d.data(), true);
    }

    // espectro em dB: Stereo = potencia media dos canais (link); M/S = cada canal separado
    const int nState = ms ? 2 : 1;
    for (int k = 0; k < numBins; ++k)
    {
        if (! ms)
        {
            float p = 0.0f;
            for (int ch = 0; ch < nCh; ++ch)
            {
                const float re = fftData[(size_t) ch][(size_t) (2 * k)];
                const float im = fftData[(size_t) ch][(size_t) (2 * k + 1)];
                p += re * re + im * im;
            }
            dbSpec[0][(size_t) k] = 10.0f * std::log10 (p / (float) nCh + 1.0e-12f);
        }
        else
        {
            for (int st = 0; st < 2; ++st)
            {
                const float re = fftData[(size_t) st][(size_t) (2 * k)];
                const float im = fftData[(size_t) st][(size_t) (2 * k + 1)];
                dbSpec[(size_t) st][(size_t) k] = 10.0f * std::log10 (re * re + im * im + 1.0e-12f);
            }
        }
    }

    for (int st = 0; st < nState; ++st)
    {
        bool active = true;
        if (ms) active = (st == 0) ? (pProc != 3) : (pProc != 2); // Mid-only / Side-only
        updateReduction (st, active);
    }

    // dados para a interface
    for (int k = 0; k < numBins; ++k)
    {
        float r = smoothed[0][(size_t) k];
        float s = dbSpec[0][(size_t) k];
        if (ms)
        {
            r = juce::jmax (r, smoothed[1][(size_t) k]);
            s = juce::jmax (s, dbSpec[1][(size_t) k]);
        }
        displayReduction[(size_t) k].store (r, std::memory_order_relaxed);
        displaySpec[(size_t) k].store (s - 54.2f, std::memory_order_relaxed); // seno 0 dBFS ~ 0 dB
    }

    // aplica os ganhos e volta ao tempo
    for (int ch = 0; ch < nCh; ++ch)
    {
        const auto& sm = smoothed[(size_t) (ms ? ch : 0)];
        auto& d = fftData[(size_t) ch];
        for (int k = 0; k < numBins; ++k)
        {
            const float g = std::exp (-sm[(size_t) k] * 0.11512925f); // 10^(-dB/20)
            d[(size_t) (2 * k)]     *= g;
            d[(size_t) (2 * k + 1)] *= g;
        }
        fft.performRealOnlyInverseTransform (d.data());
    }

    if (ms)
        for (int i = 0; i < fftSize; ++i)
        {
            const float m = fftData[0][(size_t) i], s = fftData[1][(size_t) i];
            fftData[0][(size_t) i] = m + s;
            fftData[1][(size_t) i] = m - s;
        }

    for (int ch = 0; ch < nCh; ++ch)
        for (int i = 0; i < fftSize; ++i)
            outRing[(size_t) ch][(size_t) ((pos + i) & mask)] += fftData[(size_t) ch][(size_t) i] * window[(size_t) i] * olaNorm;
}

juce::AudioProcessorEditor* CalmaProcessor::createEditor() { return new CalmaEditor (*this); }

void CalmaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void CalmaProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CalmaProcessor(); }
