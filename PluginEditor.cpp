#include "PluginEditor.h"
#include <BinaryData.h>

namespace
{
    // เลือกฟอนต์ที่รองรับภาษาไทยจริง ๆ (ฟอนต์ดีฟอลต์ของ JUCE ทำให้สระ/วรรณยุกต์เพี้ยน)
    juce::FontOptions thaiFont (float height, int style = juce::Font::plain)
    {
        static const juce::String chosen = []() -> juce::String
        {
            const auto available = juce::Font::findAllTypefaceNames();
            for (auto* name : { "Leelawadee UI", "Leelawadee", "Tahoma", "Thonburi",
                                "Noto Sans Thai", "Sarabun", "Segoe UI" })
                if (available.contains (name))
                    return name;
            return {};
        }();

        if (chosen.isEmpty())
            return juce::FontOptions (height, style);
        return juce::FontOptions (chosen, height, style);
    }

    // knob.png เป็นภาพจัตุรัส 210x210 ที่จุดกึ่งกลางฝาโลหะอยู่กลางภาพพอดี (หมุนรอบจุดนี้ ไม่แกว่ง)
    // เข็มในรูปชี้ราว -26.9 องศาจากด้านบน (เรเดียน)
    constexpr float knobPointerOffset = -0.4697f;
}

// ---------- Look and feel ----------
KnobLookAndFeel::KnobLookAndFeel()
{
    knobImage = juce::ImageCache::getFromMemory (BinaryData::knob_png, BinaryData::knob_pngSize);

    setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff1d2026));
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff3a3d45));
    setColour (juce::Label::textColourId, juce::Colour (0xffd3d3d3));
}

void KnobLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float startAngle, float endAngle,
                                        juce::Slider&)
{
    const float diameter = (float) juce::jmin (width, height);
    const float cx = (float) x + (float) width * 0.5f;
    const float cy = (float) y + (float) height * 0.5f;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    // วงแหวนแสดงค่าล้อมรอบปุ่ม
    const float ringR = diameter * 0.5f - 2.f;
    juce::Path track, value;
    track.addCentredArc (cx, cy, ringR, ringR, 0.f, startAngle, endAngle, true);
    value.addCentredArc (cx, cy, ringR, ringR, 0.f, startAngle, angle, true);
    g.setColour (juce::Colour (0xff2f333b));
    g.strokePath (track, juce::PathStrokeType (3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colour (0xff42a2c8));
    g.strokePath (value, juce::PathStrokeType (3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (knobImage.isValid())
    {
        const float imgW = (float) knobImage.getWidth();
        const float imgH = (float) knobImage.getHeight();
        const float knobD = diameter - 14.f;
        const float s = knobD / juce::jmax (imgW, imgH);

        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImageTransformed (knobImage,
                                juce::AffineTransform::translation (-imgW * 0.5f, -imgH * 0.5f)
                                    .rotated (angle - knobPointerOffset)
                                    .scaled (s)
                                    .translated (cx, cy),
                                false);
    }
}

// ---------- Editor ----------
AiVocalEQEditor::AiVocalEQEditor (AiVocalEQProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setSize (560, 430);

    style.addItemList ({ "Pop", "Rap", "Rock", "Speech" }, 1);
    addAndMakeVisible (style);
    styleAtt = std::make_unique<CA> (proc.apvts, "style", style);

    learn.setClickingTogglesState (true);
    learn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd9363e));
    addAndMakeVisible (learn);
    learnAtt = std::make_unique<BA> (proc.apvts, "learn", learn);

    auto setupKnob = [this] (juce::Slider& s, juce::Label& l, const juce::String& name)
    {
        s.setLookAndFeel (&knobLnf);
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                               juce::MathConstants<float>::pi * 2.8f, true);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
        s.setMouseDragSensitivity (200);
        addAndMakeVisible (s);

        l.setText (name, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::FontOptions (13.f, juce::Font::bold));
        l.setColour (juce::Label::textColourId, juce::Colour (0xffd3d3d3));
        addAndMakeVisible (l);
    };
    setupKnob (amount, amountL, "AI AMOUNT");
    setupKnob (air, airL, "AIR");
    setupKnob (output, outputL, "OUTPUT");

    amountAtt = std::make_unique<SA> (proc.apvts, "amount", amount);
    airAtt    = std::make_unique<SA> (proc.apvts, "air", air);
    outputAtt = std::make_unique<SA> (proc.apvts, "output", output);

    amount.setTextValueSuffix (" %");
    air.setTextValueSuffix (" dB");
    output.setTextValueSuffix (" dB");

    status.setJustificationType (juce::Justification::centredLeft);
    status.setFont (thaiFont (14.f));
    status.setColour (juce::Label::textColourId, juce::Colour (0xffd3d3d3));
    addAndMakeVisible (status);

    startTimerHz (20);
}

AiVocalEQEditor::~AiVocalEQEditor()
{
    stopTimer();
    amount.setLookAndFeel (nullptr);
    air.setLookAndFeel (nullptr);
    output.setLookAndFeel (nullptr);
}

void AiVocalEQEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff15171c));

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (24.f, juce::Font::bold));
    g.drawText ("EQ AI MIX VOCALS FULL", 20, 10, getWidth() - 40, 30, juce::Justification::centredLeft);

    g.setColour (juce::Colour (0xffe0b050));
    g.setFont (thaiFont (17.f));
    g.drawText (juce::String::fromUTF8 ("อาร์ม หนองม่วง มือเปิ่มสตูดิโอ"),
                20, 40, getWidth() - 40, 26, juce::Justification::centredLeft);

    // แสดงเกนที่ AI ปรับในแต่ละย่าน
    g.setColour (juce::Colour (0xff22252c));
    g.fillRoundedRectangle (barsArea.toFloat(), 6.f);

    const int n = AiVocalEQProcessor::numBands;
    const float colW = (float) barsArea.getWidth() / n;
    const float midY = (float) barsArea.getCentreY() - 6.f;
    const float scale = ((float) barsArea.getHeight() - 30.f) * 0.5f / 8.f; // ±8 dB เต็มกรอบ

    g.setColour (juce::Colours::grey.withAlpha (0.4f));
    g.drawHorizontalLine ((int) midY, (float) barsArea.getX() + 6.f, (float) barsArea.getRight() - 6.f);

    for (int b = 0; b < n; ++b)
    {
        const float gain = proc.bandGainDb[(size_t) b].load();
        const float h = gain * scale;
        const float x = (float) barsArea.getX() + colW * b + colW * 0.25f;
        g.setColour (gain >= 0.f ? juce::Colour (0xff4cc38a) : juce::Colour (0xffe0714c));
        g.fillRect (x, h >= 0.f ? midY - h : midY, colW * 0.5f, std::abs (h));

        g.setColour (juce::Colours::lightgrey);
        g.setFont (juce::FontOptions (12.f));
        g.drawText (juce::String (AiVocalEQProcessor::bandNames[b]) + (b == n - 1 ? "" : " Hz"),
                    (int) ((float) barsArea.getX() + colW * (float) b), barsArea.getBottom() - 22, (int) colW, 14,
                    juce::Justification::centred);
        g.drawText (juce::String (gain, 1) + " dB",
                    (int) ((float) barsArea.getX() + colW * (float) b), barsArea.getY() + 4, (int) colW, 14,
                    juce::Justification::centred);
    }
}

void AiVocalEQEditor::resized()
{
    style.setBounds (20, 76, 180, 28);
    learn.setBounds (212, 76, 100, 28);
    status.setBounds (324, 76, getWidth() - 344, 28);

    barsArea = { 20, 116, getWidth() - 40, 120 };

    const int knobW = 120, knobH = 150, labelH = 20;
    const int gap = (getWidth() - 3 * knobW) / 4;
    int x = gap;
    juce::Slider* sliders[] = { &amount, &air, &output };
    juce::Label* labels[]   = { &amountL, &airL, &outputL };
    for (int i = 0; i < 3; ++i)
    {
        labels[i]->setBounds (x, 250, knobW, labelH);
        sliders[i]->setBounds (x, 250 + labelH, knobW, knobH);
        x += knobW + gap;
    }
}

void AiVocalEQEditor::timerCallback()
{
    const bool learning = learn.getToggleState();
    const int frames = proc.learnFrames.load();

    if (learning)
        status.setText (juce::String::fromUTF8 ("กำลังเรียนรู้... ร้องต่อเนื่อง 10-20 วินาที"), juce::dontSendNotification);
    else if (frames >= 8)
        status.setText (juce::String::fromUTF8 ("พร้อมใช้งาน (เรียนรู้แล้ว)"), juce::dontSendNotification);
    else
        status.setText (juce::String::fromUTF8 ("กด LEARN แล้วเล่นเสียงร้อง"), juce::dontSendNotification);

    repaint (barsArea);
}
