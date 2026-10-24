#pragma once
#include "PluginProcessor.h"

namespace ui
{
    inline const juce::Colour bg     (0xff121317);
    inline const juce::Colour panel  (0xff1b1c22);
    inline const juce::Colour panel2 (0xff24262e);
    inline const juce::Colour line   (0xff2f323c);
    inline const juce::Colour text   (0xffeceef4);
    inline const juce::Colour dim    (0xff8a8d99);
    inline const juce::Colour accent (0xffff5c93);
    inline const juce::Colour cyan   (0xff4cc3ff);
}

// knobs modernos: arco fino + ponteiro
class ModernLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ModernLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
};

// seletor segmentado (ligado a um parametro de escolha)
class Segmented : public juce::Component
{
public:
    Segmented (juce::RangedAudioParameter& param, juce::StringArray names);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::StringArray names;
    int index = 0;
    juce::ParameterAttachment attach;
};

// display central: espectro, reducao, EQ de referencia e limites de banda
class SpectrumDisplay : public juce::Component, private juce::Timer
{
public:
    explicit SpectrumDisplay (CalmaProcessor&);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    static constexpr int nodeCount = CalmaProcessor::numEqNodes;
    static constexpr int lowHandle = nodeCount, highHandle = nodeCount + 1;

    void timerCallback() override;
    float freqToX (float f) const;
    float xToFreq (float x) const;
    float eqToY (float db) const;
    float yToEq (float y) const;
    float eqAt (float f) const;
    int hitTest (juce::Point<float>) const;
    juce::ParameterAttachment* attachmentFor (int handle);

    CalmaProcessor& proc;
    std::array<float, CalmaProcessor::numBins> specSm {}, redSm {};
    std::array<float, nodeCount> eqVal {};
    float lowVal = 100.0f, highVal = 16000.0f;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> eqAttach;
    std::unique_ptr<juce::ParameterAttachment> lowAttach, highAttach;
    int hover = -1, dragging = -1;
};

class CalmaEditor : public juce::AudioProcessorEditor
{
public:
    explicit CalmaEditor (CalmaProcessor&);
    ~CalmaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };

    CalmaProcessor& proc;
    ModernLookAndFeel lnf;
    SpectrumDisplay display;
    Segmented modeSeg, procSeg;
    juce::TextButton deltaBtn { "DELTA" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> deltaAttach;
    std::array<Knob, 8> knobs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CalmaEditor)
};
