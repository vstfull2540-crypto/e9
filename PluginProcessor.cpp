#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
    constexpr float bandFreq[AiVocalEQProcessor::numBands] = { 120.f, 300.f, 1000.f, 3500.f, 7000.f, 12000.f };
    constexpr float bandQ[AiVocalEQProcessor::numBands]    = { 0.9f, 1.0f, 1.0f, 1.1f, 2.0f, 0.7f };

    // เป้าหมายระดับความหนาแน่นสเปกตรัมของแต่ละย่าน (dB) เทียบกับย่าน 1 kHz
    // เป็นค่าเริ่มต้นโดยประมาณ ปรับจูนเพิ่มได้ตามหูและสไตล์งาน
    //                                        120   300    1k     3.5k   7k     12k
    constexpr float styleTarget[4][AiVocalEQProcessor::numBands] = {
        /* Pop    */ {  3.f,  3.f, 0.f, -10.f, -19.f, -26.f },
        /* Rap    */ {  4.f,  2.f, 0.f,  -9.f, -18.f, -25.f },
        /* Rock   */ {  2.f,  2.f, 0.f,  -9.f, -18.f, -27.f },
        /* Speech */ {  4.f,  3.f, 0.f, -11.f, -21.f, -30.f },
    };

    constexpr float maxCorrectionDb = 6.f;
}

const char* const AiVocalEQProcessor::bandNames[numBands] = { "120", "300", "1k", "3.5k", "7k", "Air" };

AiVocalEQProcessor::AiVocalEQProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (auto& g : bandGainDb) g.store (0.f);
    for (auto& l : learnedDb)  l.store (0.f);
}

juce::AudioProcessorValueTreeState::ParameterLayout AiVocalEQProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "style", 1 }, "Style",
                                                        StringArray { "Pop", "Rap", "Rock", "Speech" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "amount", 1 }, "AI Amount",
                                                       NormalisableRange<float> (0.f, 100.f, 1.f), 70.f,
                                                       AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "air", 1 }, "Air",
                                                       NormalisableRange<float> (-6.f, 6.f, 0.1f), 0.f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output",
                                                       NormalisableRange<float> (-12.f, 12.f, 0.1f), 0.f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "learn", 1 }, "Learn", false));
    return layout;
}

// ---------- RBJ biquad ----------
AiVocalEQProcessor::Coeffs AiVocalEQProcessor::makePeak (double fs, double f, double q, double gainDb)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = juce::MathConstants<double>::twoPi * f / fs;
    const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
    return { 1.0 + alpha * A, -2.0 * cw, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * cw, 1.0 - alpha / A };
}

AiVocalEQProcessor::Coeffs AiVocalEQProcessor::makeHighShelf (double fs, double f, double gainDb)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = juce::MathConstants<double>::twoPi * f / fs;
    const double cw = std::cos (w0);
    const double alpha = std::sin (w0) / 2.0 * std::sqrt (2.0);
    const double sA = 2.0 * std::sqrt (A) * alpha;
    return { A * ((A + 1) + (A - 1) * cw + sA),
             -2.0 * A * ((A - 1) + (A + 1) * cw),
             A * ((A + 1) + (A - 1) * cw - sA),
             (A + 1) - (A - 1) * cw + sA,
             2.0 * ((A - 1) - (A + 1) * cw),
             (A + 1) - (A - 1) * cw - sA };
}

AiVocalEQProcessor::Coeffs AiVocalEQProcessor::makeHighPass (double fs, double f, double q)
{
    const double w0 = juce::MathConstants<double>::twoPi * f / fs;
    const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
    return { (1.0 + cw) / 2.0, -(1.0 + cw), (1.0 + cw) / 2.0, 1.0 + alpha, -2.0 * cw, 1.0 - alpha };
}

// ---------- lifecycle ----------
void AiVocalEQProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
    for (auto& f : hpf) f.reset();
    for (auto& ch : bands) for (auto& b : ch) b.reset();
    currentGain.fill (0.f);
    fifoIndex = 0;

    for (int b = 0; b < numBands; ++b)
    {
        const double lo = bandFreq[b] / 1.41, hi = bandFreq[b] * 1.41;
        binLo[(size_t) b] = juce::jmax (1, (int) std::floor (lo * fftSize / sr));
        binHi[(size_t) b] = juce::jmin (fftSize / 2 - 1, (int) std::ceil (hi * fftSize / sr));
        if (binHi[(size_t) b] < binLo[(size_t) b]) binHi[(size_t) b] = binLo[(size_t) b];
    }
}

bool AiVocalEQProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

// ---------- analyser ("AI" learning) ----------
void AiVocalEQProcessor::analyseBlock (const juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const int chs = buffer.getNumChannels();
    for (int i = 0; i < n; ++i)
    {
        float m = 0.f;
        for (int c = 0; c < chs; ++c) m += buffer.getReadPointer (c)[i];
        fifo[(size_t) fifoIndex++] = m / (float) juce::jmax (1, chs);
        if (fifoIndex == fftSize)
        {
            processFrame();
            fifoIndex = 0;
        }
    }
}

void AiVocalEQProcessor::processFrame()
{
    double energy = 0.0;
    for (float s : fifo) energy += (double) s * s;
    const double rms = std::sqrt (energy / fftSize);
    if (rms < 0.0018) // เงียบกว่า ~ -55 dBFS ข้าม ไม่เอามาเรียนรู้
        return;

    std::copy (fifo.begin(), fifo.end(), fftData.begin());
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.f);
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    ++frameCount;
    for (int b = 0; b < numBands; ++b)
    {
        double acc = 0.0;
        const int lo = binLo[(size_t) b], hi = binHi[(size_t) b];
        for (int k = lo; k <= hi; ++k) acc += (double) fftData[(size_t) k] * fftData[(size_t) k];
        acc /= (double) (hi - lo + 1);
        sumDb[(size_t) b] += 10.0 * std::log10 (acc + 1.0e-12);
        learnedDb[(size_t) b].store ((float) (sumDb[(size_t) b] / frameCount));
    }
    learnFrames.store (frameCount);
}

// ---------- audio ----------
void AiVocalEQProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    for (int c = numIn; c < numOut; ++c) buffer.clear (c, 0, n);

    const bool learn = apvts.getRawParameterValue ("learn")->load() > 0.5f;
    if (learn && ! learnWasOn)
    {
        sumDb.fill (0.0);
        frameCount = 0;
        fifoIndex = 0;
        learnFrames.store (0);
    }
    learnWasOn = learn;
    if (learn) analyseBlock (buffer);

    const int style = (int) apvts.getRawParameterValue ("style")->load();
    const float amount = apvts.getRawParameterValue ("amount")->load() / 100.f;
    const float airDb = apvts.getRawParameterValue ("air")->load();
    const float outGain = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("output")->load());

    // เป้าหมายเกนของแต่ละย่านจากสิ่งที่เรียนรู้ได้
    std::array<float, numBands> target {};
    if (learnFrames.load() >= 8)
    {
        const float ref = learnedDb[2].load();
        for (int b = 0; b < numBands; ++b)
        {
            if (b == 2) continue; // ย่าน 1k เป็นจุดอ้างอิง
            const float measured = learnedDb[(size_t) b].load() - ref;
            const float diff = styleTarget[juce::jlimit (0, 3, style)][b] - measured;
            target[(size_t) b] = juce::jlimit (-maxCorrectionDb, maxCorrectionDb, diff) * amount;
        }
    }
    target[5] += airDb;

    const float coef = 1.f - std::exp (-(float) n / (0.25f * (float) sampleRate));
    for (int b = 0; b < numBands; ++b)
    {
        currentGain[(size_t) b] += (target[(size_t) b] - currentGain[(size_t) b]) * coef;
        bandGainDb[(size_t) b].store (currentGain[(size_t) b]);
    }

    const auto hp = makeHighPass (sampleRate, 80.0, 0.707);
    std::array<Coeffs, numBands> cf;
    for (int b = 0; b < numBands; ++b)
        cf[(size_t) b] = (b == 5) ? makeHighShelf (sampleRate, bandFreq[b], currentGain[(size_t) b])
                                  : makePeak (sampleRate, bandFreq[b], bandQ[b], currentGain[(size_t) b]);

    const int chs = juce::jmin (buffer.getNumChannels(), 2);
    for (int c = 0; c < chs; ++c)
    {
        hpf[(size_t) c].set (hp);
        for (int b = 0; b < numBands; ++b) bands[(size_t) c][(size_t) b].set (cf[(size_t) b]);

        float* d = buffer.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            float x = hpf[(size_t) c].process (d[i]);
            for (int b = 0; b < numBands; ++b) x = bands[(size_t) c][(size_t) b].process (x);
            d[i] = x * outGain;
        }
    }
}

// ---------- state ----------
void AiVocalEQProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    juce::String s;
    for (int b = 0; b < numBands; ++b) s << juce::String (learnedDb[(size_t) b].load()) << " ";
    state.setProperty ("learned", s.trim(), nullptr);
    state.setProperty ("learnFrames", learnFrames.load(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void AiVocalEQProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        if (! xml->hasTagName (apvts.state.getType())) return;
        auto vt = juce::ValueTree::fromXml (*xml);
        const auto tokens = juce::StringArray::fromTokens (vt.getProperty ("learned").toString(), " ", "");
        if (tokens.size() == numBands)
        {
            for (int b = 0; b < numBands; ++b) learnedDb[(size_t) b].store (tokens[b].getFloatValue());
            learnFrames.store ((int) vt.getProperty ("learnFrames", 0));
        }
        apvts.replaceState (vt);
        if (auto* p = apvts.getParameter ("learn")) p->setValueNotifyingHost (0.f); // กันเรียนรู้ทับค่าที่โหลดมา
    }
}

juce::AudioProcessorEditor* AiVocalEQProcessor::createEditor() { return new AiVocalEQEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AiVocalEQProcessor(); }
