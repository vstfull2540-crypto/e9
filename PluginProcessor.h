#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

// EQ AI Mix Vocals Full
// 6-band vocal EQ (HPF + 6 bands) ที่ "เรียนรู้" สเปกตรัมของเสียงร้องจริง
// แล้วปรับเกนแต่ละย่านเข้าหาเป้าหมายตามสไตล์ (Pop / Rap / Rock / Speech)
class AiVocalEQProcessor : public juce::AudioProcessor
{
public:
    static constexpr int numBands = 6;
    static constexpr int fftOrder = 11;
    static constexpr int fftSize  = 1 << fftOrder;

    AiVocalEQProcessor();
    ~AiVocalEQProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "EQ AI Mix Vocals Full"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    // ให้ UI อ่านเพื่อแสดงผล
    std::array<std::atomic<float>, numBands> bandGainDb;
    std::array<std::atomic<float>, numBands> learnedDb;
    std::atomic<int> learnFrames { 0 };

    static const char* const bandNames[numBands];

private:
    struct Coeffs { double b0, b1, b2, a0, a1, a2; };

    struct Biquad
    {
        float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f, z1 = 0.f, z2 = 0.f;

        void set (const Coeffs& c)
        {
            b0 = (float) (c.b0 / c.a0);
            b1 = (float) (c.b1 / c.a0);
            b2 = (float) (c.b2 / c.a0);
            a1 = (float) (c.a1 / c.a0);
            a2 = (float) (c.a2 / c.a0);
        }
        void reset() { z1 = z2 = 0.f; }
        float process (float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    static Coeffs makePeak (double fs, double f, double q, double gainDb);
    static Coeffs makeHighShelf (double fs, double f, double gainDb);
    static Coeffs makeHighPass (double fs, double f, double q);

    void analyseBlock (const juce::AudioBuffer<float>& buffer);
    void processFrame();

    double sampleRate = 44100.0;

    std::array<Biquad, 2> hpf;
    std::array<std::array<Biquad, numBands>, 2> bands;
    std::array<float, numBands> currentGain {};

    // analyser
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, fftSize> fifo {};
    std::array<float, 2 * fftSize> fftData {};
    int fifoIndex = 0;
    std::array<double, numBands> sumDb {};
    int frameCount = 0;
    bool learnWasOn = false;
    std::array<int, numBands> binLo {}, binHi {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AiVocalEQProcessor)
};
