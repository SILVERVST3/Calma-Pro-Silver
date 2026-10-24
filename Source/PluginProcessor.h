#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

// CALMA - supressor dinamico de ressonancias (STFT, 2048 pts, 75% overlap)
class CalmaProcessor : public juce::AudioProcessor
{
public:
    static constexpr int fftOrder   = 11;
    static constexpr int fftSize    = 1 << fftOrder;
    static constexpr int hopSize    = fftSize / 4;
    static constexpr int numBins    = fftSize / 2 + 1;
    static constexpr int maxCh      = 2;
    static constexpr int numEqNodes = 7;
    static constexpr std::array<float, numEqNodes> eqFreqs { 60.0f, 150.0f, 400.0f, 1000.0f, 2500.0f, 6000.0f, 14000.0f };

    CalmaProcessor();
    ~CalmaProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "CALMA"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // dados para a interface: reducao (dB, positiva) e espectro de entrada (dBFS) por bin
    std::array<std::atomic<float>, numBins> displayReduction;
    std::array<std::atomic<float>, numBins> displaySpec;

private:
    using Spec = std::array<float, numBins>;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void processFrame (int nCh);
    void updateReduction (int st, bool active);

    juce::dsp::FFT fft { fftOrder };

    std::array<std::array<float, fftSize>, maxCh>     inRing {};
    std::array<std::array<float, fftSize>, maxCh>     outRing {};
    std::array<std::array<float, 2 * fftSize>, maxCh> fftData {};
    std::array<float, fftSize> window {};
    std::array<Spec, maxCh> dbSpec {}, reduction {}, smoothed {};
    std::array<double, numBins + 1> prefix {};
    std::array<int, numBins> eqSeg {};
    Spec eqT {};

    float olaNorm = 1.0f;
    int pos = 0, hopCount = 0;
    double fs = 44100.0;

    // ponteiros para parametros (lidos a cada bloco)
    std::atomic<float> *prDepth, *prSens, *prSharp, *prSelect, *prAttack, *prRelease,
                       *prLow, *prHigh, *prMix, *prTrim, *prDelta, *prMode, *prProc;
    std::array<std::atomic<float>*, numEqNodes> prEq {};

    // valores do bloco atual
    float pDepth = 0.5f, pSens = 0.5f, pSharp = 0.5f, pSelect = 0.7f, pMix = 1.0f, pTrim = 1.0f;
    float pLow = 20.0f, pHigh = 20000.0f;
    float attCoef = 0.0f, relCoef = 0.0f;
    int   pMode = 0, pProc = 0;
    bool  pDelta = false;
    std::array<float, numEqNodes> eqNode {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CalmaProcessor)
};
