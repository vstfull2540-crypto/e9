#pragma once

#include "PluginProcessor.h"

// ปุ่มหมุนใช้รูปปุ่มโลหะ (knob.png) หมุนตามค่าพารามิเตอร์ + วงแหวนแสดงค่า
class KnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KnobLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;

private:
    juce::Image knobImage;
};

class AiVocalEQEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AiVocalEQEditor (AiVocalEQProcessor&);
    ~AiVocalEQEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    AiVocalEQProcessor& proc;

    KnobLookAndFeel knobLnf; // ต้องประกาศก่อน slider เพื่อให้ถูกทำลายทีหลัง

    juce::ComboBox style;
    juce::TextButton learn { "LEARN" };
    juce::Slider amount, air, output;
    juce::Label amountL, airL, outputL, status;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<CA> styleAtt;
    std::unique_ptr<SA> amountAtt, airAtt, outputAtt;
    std::unique_ptr<BA> learnAtt;

    juce::Rectangle<int> barsArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AiVocalEQEditor)
};
