#include "NRRender.h"
#include "NR.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdlib>
#include <iomanip>
#include <iostream>

namespace ksm
{
namespace
{
class BandRms
{
public:
    explicit BandRms(double rate)
    {
        NR bank; bank.prepare(rate);
        for (size_t b = 0; b < coefficient.size(); ++b)
            coefficient[b] = 1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * bank.bandUpperFrequency(b) / rate);
    }
    void push(float left, float right)
    {
        const std::array<double, 2> input {left, right};
        for (size_t c = 0; c < 2; ++c)
        {
            double previous = 0.0;
            for (size_t b = 0; b < coefficient.size(); ++b)
            {
                lowpass[c][b] += coefficient[b] * (input[c] - lowpass[c][b]);
                const auto band = lowpass[c][b] - previous;
                energy[b] += band * band; previous = lowpass[c][b];
            }
            const auto high = input[c] - previous;
            energy[NR::bandCount - 1] += high * high;
        }
        ++frames;
    }
    double rms(size_t b) const { return frames > 0 ? std::sqrt(energy[b] / (2.0 * static_cast<double>(frames))) : 0.0; }
private:
    std::array<double, NR::bandCount - 1> coefficient {};
    std::array<std::array<double, NR::bandCount - 1>, 2> lowpass {};
    std::array<double, NR::bandCount> energy {};
    int64_t frames = 0;
};
}
int renderNR(const juce::StringArray& arguments)
{
    const int index = arguments.indexOf("--nr-render");
    if (index < 0 || index + 5 != arguments.size())
    {
        std::cerr << "Usage: --nr-render input.wav noise.wav output.wav amountDb\n"; return 2;
    }
    const auto amountText = arguments[index + 4].toStdString();
    char* end = nullptr; const double amount = std::strtod(amountText.c_str(), &end);
    if (end == amountText.c_str() || *end != '\0' || !std::isfinite(amount) || amount < 0.0 || amount > 18.0)
    { std::cerr << "amountDb must be between 0 and 18\n"; return 2; }
    const auto current = juce::File::getCurrentWorkingDirectory();
    const auto inputFile = current.getChildFile(arguments[index + 1]);
    const auto noiseFile = current.getChildFile(arguments[index + 2]);
    const auto outputFile = current.getChildFile(arguments[index + 3]);
    if (outputFile.exists()) { std::cerr << "Output already exists; choose a new path\n"; return 2; }
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> input(formats.createReaderFor(inputFile)), noise(formats.createReaderFor(noiseFile));
    if (input == nullptr || noise == nullptr || input->numChannels == 0 || input->numChannels > 2 || noise->numChannels == 0 || noise->numChannels > 2)
    { std::cerr << "Input and noise must be readable mono or stereo audio\n"; return 2; }
    if (std::abs(input->sampleRate - noise->sampleRate) > 0.01 || input->sampleRate < 8000.0 || input->sampleRate > 192000.0
        || noise->lengthInSamples < static_cast<int64_t>(std::round(noise->sampleRate)) || input->lengthInSamples == 0)
    { std::cerr << "Rates must match (8-192 kHz); noise needs at least one second and input must not be empty\n"; return 2; }
    NR learner; learner.reductionDecibels.store(static_cast<float>(amount)); learner.prepare(input->sampleRate); learner.requestLearn();
    juce::AudioBuffer<float> buffer(2, 1024);
    const auto learnFrames = static_cast<int64_t>(std::round(noise->sampleRate));
    for (int64_t position = 0; position < learnFrames;)
    {
        const int count = static_cast<int>(std::min<int64_t>(1024, learnFrames - position));
        if (!noise->read(&buffer, 0, count, position, true, true)) { std::cerr << "Noise read failed\n"; return 1; }
        learner.beginBlock();
        for (int f = 0; f < count; ++f)
        {
            float l = buffer.getSample(0, f), r = noise->numChannels == 1 ? l : buffer.getSample(1, f);
            learner.process(l, r);
        }
        position += count;
    }
    // Use the same serialized profile path as settings and projects.
    const auto profileText = juce::JSON::toString(learner.saveState(), true);
    NR nr;
    if (auto result = nr.restoreState(juce::JSON::parse(profileText)); result.failed())
    { std::cerr << result.getErrorMessage() << '\n'; return 1; }
    nr.prepare(input->sampleRate); nr.beginBlock(); nr.enabled.store(true);
    std::unique_ptr<juce::OutputStream> output = outputFile.createOutputStream();
    if (output == nullptr) { std::cerr << "Output cannot be opened\n"; return 1; }
    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions{}.withSampleRate(input->sampleRate)
        .withNumChannels(static_cast<int>(input->numChannels)).withBitsPerSample(32)
        .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = wav.createWriterFor(output, options);
    if (writer == nullptr) { std::cerr << "WAV writer cannot be opened\n"; return 1; }
    BandRms before(input->sampleRate), after(input->sampleRate);
    int64_t firstInput = -1, firstOutput = -1;
    for (int64_t position = 0; position < input->lengthInSamples;)
    {
        const int count = static_cast<int>(std::min<int64_t>(1024, input->lengthInSamples - position));
        if (!input->read(&buffer, 0, count, position, true, true)) { std::cerr << "Input read failed\n"; return 1; }
        nr.beginBlock();
        for (int f = 0; f < count; ++f)
        {
            float l = buffer.getSample(0, f), r = input->numChannels == 1 ? l : buffer.getSample(1, f);
            if (firstInput < 0 && std::max(std::abs(l), std::abs(r)) > 1.0e-7f) firstInput = position + f;
            before.push(l, r); nr.process(l, r); after.push(l, r);
            if (firstOutput < 0 && std::max(std::abs(l), std::abs(r)) > 1.0e-7f) firstOutput = position + f;
            buffer.setSample(0, f, l); buffer.setSample(1, f, r);
        }
        if (!writer->writeFromAudioSampleBuffer(buffer, 0, count)) { std::cerr << "WAV write failed\n"; return 1; }
        position += count;
    }
    writer.reset();
    std::cout << "NR_RENDER rate=" << input->sampleRate << " channels=" << input->numChannels << " frames=" << input->lengthInSamples
              << " amountDb=" << amount << " learnedFrames=" << learnFrames << " latencySamples=" << NR::latencySamples
              << " firstInput=" << firstInput << " firstOutput=" << firstOutput << '\n';
    std::cout << "band,upperHz,inputRms,outputRms,changeDb\n" << std::setprecision(9);
    for (size_t b = 0; b < NR::bandCount; ++b)
        std::cout << b + 1 << ',' << nr.bandUpperFrequency(b) << ',' << before.rms(b) << ',' << after.rms(b) << ','
                  << 20.0 * std::log10(std::max(1.0e-15, after.rms(b)) / std::max(1.0e-15, before.rms(b))) << '\n';
    return 0;
}
}
