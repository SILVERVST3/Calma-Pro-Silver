#include "PluginEditor.h"
#include <cmath>

// ============================================================ LookAndFeel
ModernLookAndFeel::ModernLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, ui::text);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, ui::dim);
    setColour (juce::TextButton::buttonColourId, ui::panel);
    setColour (juce::TextButton::buttonOnColourId, ui::accent);
    setColour (juce::TextButton::textColourOffId, ui::dim);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
}

void ModernLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                          float startAngle, float endAngle, juce::Slider&)
{
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float radius = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float lw = 4.0f;
    const float arcR = radius - lw * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (ui::line);
    g.strokePath (track, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (pos > 0.002f)
    {
        juce::Path val;
        val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setColour (ui::accent);
        g.strokePath (val, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float bodyR = radius - lw - 5.0f;
    g.setGradientFill (juce::ColourGradient (ui::panel2.brighter (0.15f), c.x, c.y - bodyR,
                                             ui::panel, c.x, c.y + bodyR, false));
    g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);

    g.setColour (ui::text);
    const auto p1 = c.getPointOnCircumference (bodyR * 0.35f, angle);
    const auto p2 = c.getPointOnCircumference (bodyR * 0.85f, angle);
    g.drawLine (juce::Line<float> (p1, p2), 2.2f);
}

// ============================================================ Segmented
Segmented::Segmented (juce::RangedAudioParameter& param, juce::StringArray n)
    : names (std::move (n)),
      attach (param, [this] (float v) { index = juce::roundToInt (v); repaint(); })
{
    attach.sendInitialUpdate();
}

void Segmented::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (ui::panel);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (ui::line);
    g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);

    const float w = r.getWidth() / (float) names.size();
    for (int i = 0; i < names.size(); ++i)
    {
        auto seg = juce::Rectangle<float> (r.getX() + (float) i * w, r.getY(), w, r.getHeight()).reduced (3.0f);
        if (i == index)
        {
            g.setColour (ui::accent);
            g.fillRoundedRectangle (seg, seg.getHeight() * 0.5f);
        }
        g.setColour (i == index ? juce::Colours::white : ui::dim);
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText (names[i], seg, juce::Justification::centred);
    }
}

void Segmented::mouseDown (const juce::MouseEvent& e)
{
    const int i = juce::jlimit (0, names.size() - 1, (int) (e.position.x / ((float) getWidth() / (float) names.size())));
    attach.setValueAsCompleteGesture ((float) i);
}

// ============================================================ SpectrumDisplay
SpectrumDisplay::SpectrumDisplay (CalmaProcessor& p) : proc (p)
{
    specSm.fill (-120.0f);
    redSm.fill (0.0f);

    for (int i = 0; i < nodeCount; ++i)
    {
        auto* param = proc.apvts.getParameter ("eq" + juce::String (i));
        eqAttach.push_back (std::make_unique<juce::ParameterAttachment> (*param,
            [this, i] (float v) { eqVal[(size_t) i] = v; repaint(); }));
        eqAttach.back()->sendInitialUpdate();
    }
    lowAttach  = std::make_unique<juce::ParameterAttachment> (*proc.apvts.getParameter ("low"),
        [this] (float v) { lowVal = v; repaint(); });
    highAttach = std::make_unique<juce::ParameterAttachment> (*proc.apvts.getParameter ("high"),
        [this] (float v) { highVal = v; repaint(); });
    lowAttach->sendInitialUpdate();
    highAttach->sendInitialUpdate();

    startTimerHz (30);
}

void SpectrumDisplay::timerCallback()
{
    for (size_t k = 0; k < (size_t) CalmaProcessor::numBins; ++k)
    {
        const float s = proc.displaySpec[k].load (std::memory_order_relaxed);
        specSm[k] = s > specSm[k] ? s : specSm[k] + (s - specSm[k]) * 0.12f;
        const float r = proc.displayReduction[k].load (std::memory_order_relaxed);
        redSm[k] += (r - redSm[k]) * 0.45f;
    }
    repaint();
}

float SpectrumDisplay::freqToX (float f) const
{
    return (float) getWidth() * std::log10 (juce::jmax (f, 20.0f) / 20.0f) / 3.0f;
}
float SpectrumDisplay::xToFreq (float x) const
{
    return 20.0f * std::pow (1000.0f, x / (float) getWidth());
}
float SpectrumDisplay::eqToY (float db) const
{
    return (float) getHeight() * (0.5f - db / 12.0f * 0.38f);
}
float SpectrumDisplay::yToEq (float y) const
{
    return (0.5f - y / (float) getHeight()) / 0.38f * 12.0f;
}

float SpectrumDisplay::eqAt (float f) const
{
    const auto& fr = CalmaProcessor::eqFreqs;
    if (f <= fr[0]) return eqVal[0];
    if (f >= fr.back()) return eqVal[nodeCount - 1];
    int s = 0;
    while (s < nodeCount - 2 && f >= fr[(size_t) s + 1]) ++s;
    float t = std::log (f / fr[(size_t) s]) / std::log (fr[(size_t) s + 1] / fr[(size_t) s]);
    t = t * t * (3.0f - 2.0f * t);
    return eqVal[(size_t) s] * (1.0f - t) + eqVal[(size_t) s + 1] * t;
}

juce::ParameterAttachment* SpectrumDisplay::attachmentFor (int h)
{
    if (h == lowHandle)  return lowAttach.get();
    if (h == highHandle) return highAttach.get();
    return eqAttach[(size_t) h].get();
}

int SpectrumDisplay::hitTest (juce::Point<float> p) const
{
    for (int i = 0; i < nodeCount; ++i)
    {
        const juce::Point<float> c (freqToX (CalmaProcessor::eqFreqs[(size_t) i]), eqToY (eqVal[(size_t) i]));
        if (c.getDistanceFrom (p) <= 11.0f) return i;
    }
    if (std::abs (p.x - freqToX (lowVal))  <= 6.0f) return lowHandle;
    if (std::abs (p.x - freqToX (highVal)) <= 6.0f) return highHandle;
    return -1;
}

void SpectrumDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitTest (e.position);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= lowHandle ? juce::MouseCursor::LeftRightResizeCursor
                    : h >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
}

void SpectrumDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragging = hitTest (e.position);
    if (dragging >= 0) attachmentFor (dragging)->beginGesture();
}

void SpectrumDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    auto* a = attachmentFor (dragging);
    if (dragging < nodeCount)
    {
        const float v = std::round (juce::jlimit (-12.0f, 12.0f, yToEq (e.position.y)) * 10.0f) / 10.0f;
        a->setValueAsPartOfGesture (v);
    }
    else
    {
        float f = juce::jlimit (20.0f, 20000.0f, xToFreq (e.position.x));
        if (dragging == lowHandle)  f = juce::jmin (f, highVal * 0.8f);
        else                        f = juce::jmax (f, lowVal * 1.25f);
        a->setValueAsPartOfGesture (std::round (f));
    }
}

void SpectrumDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0) attachmentFor (dragging)->endGesture();
    dragging = -1;
}

void SpectrumDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = hitTest (e.position);
    if (h < 0) return;
    const float def = h < nodeCount ? 0.0f : (h == lowHandle ? 100.0f : 16000.0f);
    attachmentFor (h)->setValueAsCompleteGesture (def);
}

void SpectrumDisplay::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const float W = r.getWidth(), H = r.getHeight();
    const double fsd = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 44100.0;
    const float fs = (float) fsd;
    constexpr int N = CalmaProcessor::fftSize, K = CalmaProcessor::numBins;

    juce::Path clip;
    clip.addRoundedRectangle (r, 12.0f);
    g.setColour (ui::panel);
    g.fillPath (clip);
    g.saveState();
    g.reduceClipRegion (clip);

    // grade
    g.setFont (11.0f);
    const float gridF[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
    const char* gridN[] = { "50", "100", "200", "500", "1k", "2k", "5k", "10k" };
    for (int i = 0; i < 8; ++i)
    {
        const float x = freqToX (gridF[i]);
        g.setColour (ui::line.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, 0.0f, H);
        g.setColour (ui::dim);
        g.drawText (gridN[i], (int) x + 4, (int) H - 18, 40, 14, juce::Justification::left);
    }
    for (int i = 1; i < 5; ++i)
    {
        g.setColour (ui::line.withAlpha (0.4f));
        g.drawHorizontalLine ((int) (H * (float) i / 5.0f), 0.0f, W);
    }

    // amostra um array de bins para um pixel (interpolacao ou maximo)
    auto sample = [&] (const std::array<float, K>& arr, float x) {
        const float f1 = xToFreq (x), f2 = xToFreq (x + 2.0f);
        const int b1 = juce::jlimit (0, K - 1, (int) std::floor (f1 * (float) N / fs));
        const int b2 = juce::jlimit (0, K - 1, (int) std::ceil  (f2 * (float) N / fs));
        if (b2 - b1 <= 1)
        {
            const float fb = f1 * (float) N / fs;
            const int i = juce::jlimit (0, K - 2, (int) fb);
            const float fr = juce::jlimit (0.0f, 1.0f, fb - (float) i);
            return arr[(size_t) i] * (1.0f - fr) + arr[(size_t) i + 1] * fr;
        }
        float m = arr[(size_t) b1];
        for (int b = b1 + 1; b <= b2; ++b) m = juce::jmax (m, arr[(size_t) b]);
        return m;
    };

    // espectro de entrada (com inclinacao de 4.5 dB/oitava para ficar plano como no soothe)
    juce::Path spec;
    spec.startNewSubPath (0.0f, H);
    for (float x = 0.0f; x <= W; x += 2.0f)
    {
        const float f = xToFreq (x);
        const float v = sample (specSm, x) + 4.5f * std::log2 (f / 1000.0f);
        spec.lineTo (x, H - juce::jlimit (0.0f, 1.0f, (v + 100.0f) / 90.0f) * H * 0.92f);
    }
    spec.lineTo (W, H);
    spec.closeSubPath();
    g.setGradientFill (juce::ColourGradient (ui::cyan.withAlpha (0.45f), 0.0f, 0.0f,
                                             ui::cyan.withAlpha (0.02f), 0.0f, H, false));
    g.fillPath (spec);
    g.setColour (ui::cyan.withAlpha (0.85f));
    g.strokePath (spec, juce::PathStrokeType (1.3f));

    // reducao pendurada no topo
    juce::Path red;
    red.startNewSubPath (0.0f, 0.0f);
    for (float x = 0.0f; x <= W; x += 2.0f)
        red.lineTo (x, juce::jlimit (0.0f, 1.0f, sample (redSm, x) / 18.0f) * H * 0.45f);
    red.lineTo (W, 0.0f);
    red.closeSubPath();
    g.setGradientFill (juce::ColourGradient (ui::accent.withAlpha (0.65f), 0.0f, 0.0f,
                                             ui::accent.withAlpha (0.10f), 0.0f, H * 0.45f, false));
    g.fillPath (red);
    g.setColour (ui::accent);
    g.strokePath (red, juce::PathStrokeType (1.5f));

    // EQ de referencia
    g.setColour (ui::dim.withAlpha (0.5f));
    g.drawHorizontalLine ((int) eqToY (0.0f), 0.0f, W);
    juce::Path eq;
    for (float x = 0.0f; x <= W; x += 3.0f)
    {
        const float y = eqToY (eqAt (xToFreq (x)));
        if (x == 0.0f) eq.startNewSubPath (x, y); else eq.lineTo (x, y);
    }
    g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.strokePath (eq, juce::PathStrokeType (1.4f));

    for (int i = 0; i < nodeCount; ++i)
    {
        const juce::Point<float> c (freqToX (CalmaProcessor::eqFreqs[(size_t) i]), eqToY (eqVal[(size_t) i]));
        const bool active = (hover == i || dragging == i);
        const float rad = active ? 7.0f : 5.0f;
        g.setColour (ui::bg);
        g.fillEllipse (c.x - rad - 2, c.y - rad - 2, (rad + 2) * 2, (rad + 2) * 2);
        g.setColour (active ? ui::accent : juce::Colours::white);
        g.fillEllipse (c.x - rad, c.y - rad, rad * 2, rad * 2);
        if (active)
        {
            g.setColour (ui::text);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            const float v = eqVal[(size_t) i];
            g.drawText ((v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB",
                        (int) c.x - 40, (int) c.y - 28, 80, 16, juce::Justification::centred);
        }
    }

    // limites de banda
    const float lx = freqToX (lowVal), hx = freqToX (highVal);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRect (0.0f, 0.0f, lx, H);
    g.fillRect (hx, 0.0f, W - hx, H);
    for (int h = lowHandle; h <= highHandle; ++h)
    {
        const float x = h == lowHandle ? lx : hx;
        const bool active = (hover == h || dragging == h);
        g.setColour (active ? ui::accent : ui::text.withAlpha (0.55f));
        g.drawVerticalLine ((int) x, 0.0f, H);
        g.fillRoundedRectangle (x - 4.0f, H * 0.5f - 14.0f, 8.0f, 28.0f, 3.0f);
    }

    g.restoreState();
    g.setColour (ui::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
}

// ============================================================ Editor
CalmaEditor::CalmaEditor (CalmaProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (p),
      modeSeg (*p.apvts.getParameter ("mode"), juce::StringArray { "SOFT", "HARD" }),
      procSeg (*p.apvts.getParameter ("proc"), juce::StringArray { "STEREO", "M/S", "MID", "SIDE" })
{
    setLookAndFeel (&lnf);

    struct Def { const char* id; const char* name; };
    const Def defs[8] = { { "depth", "DEPTH" }, { "sens", "SENSITIVITY" }, { "sharp", "SHARPNESS" },
                          { "select", "SELECTIVITY" }, { "attack", "ATTACK" }, { "release", "RELEASE" },
                          { "mix", "MIX" }, { "trim", "TRIM" } };

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& k = knobs[i];
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
        k.slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        k.slider.setDoubleClickReturnValue (true, 0.0); // sobrescrito pelo attachment (valor padrao)
        k.label.setText (defs[i].name, juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        k.label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        addAndMakeVisible (k.slider);
        addAndMakeVisible (k.label);
        k.attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, defs[i].id, k.slider);
    }

    deltaBtn.setClickingTogglesState (true);
    deltaAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "delta", deltaBtn);

    addAndMakeVisible (display);
    addAndMakeVisible (modeSeg);
    addAndMakeVisible (procSeg);
    addAndMakeVisible (deltaBtn);

    setSize (920, 610);
}

CalmaEditor::~CalmaEditor() { setLookAndFeel (nullptr); }

void CalmaEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (ui::bg.brighter (0.06f), 0.0f, 0.0f,
                                             ui::bg, 0.0f, (float) getHeight(), false));
    g.fillAll();

    g.setColour (ui::accent);
    g.fillEllipse (18.0f, 25.0f, 10.0f, 10.0f);
    g.setColour (ui::text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("CALMA", 36, 16, 120, 28, juce::Justification::left);
    g.setColour (ui::dim);
    g.setFont (juce::FontOptions (10.5f, juce::Font::plain));
    g.drawText ("RESONANCE SUPPRESSOR", 130, 22, 200, 16, juce::Justification::left);
}

void CalmaEditor::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto header = r.removeFromTop (44);
    deltaBtn.setBounds (header.removeFromRight (80).reduced (0, 7));
    header.removeFromRight (10);
    procSeg.setBounds (header.removeFromRight (260).reduced (0, 7));
    header.removeFromRight (10);
    modeSeg.setBounds (header.removeFromRight (140).reduced (0, 7));

    r.removeFromTop (8);
    display.setBounds (r.removeFromTop (340));
    r.removeFromTop (10);

    const int w = r.getWidth() / (int) knobs.size();
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto cell = r.withX (r.getX() + (int) i * w).withWidth (w);
        knobs[i].label.setBounds (cell.removeFromBottom (18));
        knobs[i].slider.setBounds (cell);
    }
}
