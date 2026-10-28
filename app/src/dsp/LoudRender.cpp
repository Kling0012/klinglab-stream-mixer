#include "LoudRender.h"
#include "Loudness.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdlib>
#include <iomanip>
#include <iostream>

namespace ksm
{
int renderLoud(const juce::StringArray& arguments)
{
    const int index = arguments.indexOf("--loud-render");
    if (index < 0 || index + 5 != arguments.size())
    { std::cerr << "Usage: --loud-render input.wav output.wav STREAM|CD|OFF learnSeconds\n"; return 2; }
    const auto mode = arguments[index + 3].toUpperCase();
    const auto secondsText = arguments[index + 4].toStdString(); char* end = nullptr;
    const double seconds = std::strtod(secondsText.c_str(), &end);
    if ((mode != "STREAM" && mode != "CD" && mode != "OFF") || end == secondsText.c_str() || *end != '\0'
        || !std::isfinite(seconds) || seconds <= 0.0 || seconds > 20.0)
    { std::cerr << "mode must be STREAM, CD or OFF; learnSeconds must be >0 and <=20\n"; return 2; }
    const auto current = juce::File::getCurrentWorkingDirectory();
    const auto inputFile = current.getChildFile(arguments[index + 1]), outputFile = current.getChildFile(arguments[index + 2]);
    if (outputFile.exists()) { std::cerr << "Output already exists; choose a new path\n"; return 2; }
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(inputFile));
    if (reader == nullptr || reader->numChannels < 1 || reader->numChannels > 2 || reader->lengthInSamples <= 0
        || reader->sampleRate < 32000.0 || reader->sampleRate > 192000.0)
    { std::cerr << "Input must be readable mono/stereo audio at 32-192 kHz\n"; return 2; }
    constexpr int blockSize = 1024;
    juce::AudioBuffer<float> buffer(2, blockSize);
    Loudness processor; processor.prepare(reader->sampleRate); processor.requestLearn();
    const auto learnFrames = std::min(reader->lengthInSamples, static_cast<int64_t>(std::round(seconds * reader->sampleRate)));
    for (int64_t position = 0; position < learnFrames;)
    {
        const auto count = static_cast<int>(std::min<int64_t>(blockSize, learnFrames - position));
        if (!reader->read(&buffer, 0, count, position, true, true)) { std::cerr << "Input read failed\n"; return 1; }
        processor.beginBlock();
        for (int f = 0; f < count; ++f)
        {
            float l = buffer.getSample(0, f), r = reader->numChannels == 1 ? l : buffer.getSample(1, f);
            if (!std::isfinite(l) || !std::isfinite(r)) { std::cerr << "Non-finite input\n"; return 2; }
            processor.processBus(l, r, 1.0f); processor.processOutput(l, r);
        }
        position += count;
    }
    processor.requestStop(); processor.beginBlock();
    const bool quiet = processor.tooQuiet();
    const auto state = juce::JSON::parse(juce::JSON::toString(processor.saveState(), true));
    if (auto result = processor.restoreState(state); result.failed()) { std::cerr << result.getErrorMessage() << '\n'; return 1; }
    processor.mode.store(mode == "CD" ? Loudness::Mode::cd : Loudness::Mode::stream);
    processor.enabled.store(mode != "OFF"); processor.prepare(reader->sampleRate);
    const int latency = processor.latencySamples();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> output = outputFile.createOutputStream();
    if (output == nullptr) { std::cerr << "Output cannot be opened\n"; return 1; }
    const auto options = juce::AudioFormatWriterOptions{}.withSampleRate(reader->sampleRate).withNumChannels(2).withBitsPerSample(32)
        .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = wav.createWriterFor(output, options);
    if (writer == nullptr) { std::cerr << "WAV writer cannot be opened\n"; return 1; }
    LoudnessMeter before, after; before.prepare(reader->sampleRate); after.prepare(reader->sampleRate);
    TruePeakDetector inputPeak, outputPeak; inputPeak.prepare(); outputPeak.prepare();
    float peakBefore = 0.0f, peakAfter = 0.0f, reduction = 0.0f;
    int64_t written = 0;
    // Trim the known stream-only delay and flush the delayed input.
    for (int64_t position = 0; position < reader->lengthInSamples + latency;)
    {
        const int count = static_cast<int>(std::min<int64_t>(blockSize, reader->lengthInSamples + latency - position));
        buffer.clear();
        const int available = static_cast<int>(std::max<int64_t>(0, std::min<int64_t>(count, reader->lengthInSamples - position)));
        if (available > 0 && !reader->read(&buffer, 0, available, position, true, true)) { std::cerr << "Input read failed\n"; return 1; }
        processor.beginBlock();
        for (int f = 0; f < count; ++f)
        {
            float l = buffer.getSample(0, f), r = reader->numChannels == 1 ? l : buffer.getSample(1, f);
            if (!std::isfinite(l) || !std::isfinite(r)) { std::cerr << "Non-finite input\n"; return 2; }
            if (f < available) before.push(l, r);
            peakBefore = std::max(peakBefore, inputPeak.push(l, r));
            processor.processBus(l, r, 1.0f); processor.processOutput(l, r);
            buffer.setSample(0, f, l); buffer.setSample(1, f, r);
            if (position + f >= latency)
            { after.push(l, r); peakAfter = std::max(peakAfter, outputPeak.push(l, r)); }
            reduction = std::max(reduction, processor.reductionDecibels());
        }
        const int start = static_cast<int>(std::max<int64_t>(0, latency - position));
        if (start < count)
        {
            if (!writer->writeFromAudioSampleBuffer(buffer, start, count - start)) { std::cerr << "WAV write failed\n"; return 1; }
            written += count - start;
        }
        position += count;
    }
    // A short zero tail makes the file boundary explicit to resampling readers.
    // OFF keeps the exact input length and samples.
    const int tailFrames = mode == "OFF" ? 0 : 64;
    buffer.clear();
    if (tailFrames > 0 && !writer->writeFromAudioSampleBuffer(buffer, 0, tailFrames))
    { std::cerr << "WAV tail write failed\n"; return 1; }
    written += tailFrames;
    for (int f = 0; f < TruePeakDetector::taps; ++f)
        peakAfter = std::max(peakAfter, outputPeak.push(0.0f, 0.0f));
    writer.reset();
    const auto db = [](float peak) { return 20.0f * std::log10(std::max(1.0e-12f, peak)); };
    std::cout << std::fixed << std::setprecision(4) << "LOUD_RENDER mode=" << mode << " rate=" << reader->sampleRate
              << " channels=2 frames=" << written << " learnFrames=" << learnFrames << " learnStatus=" << (quiet ? "TOO_QUIET" : "OK")
              << " learnedDb=" << processor.learnedDecibels.load() << " latencySamples=" << latency
              << " inputLUFS=" << before.integrated() << " outputLUFS=" << after.integrated()
              << " inputDbTP=" << db(peakBefore) << " outputDbTP=" << db(peakAfter) << " maxReductionDb=" << reduction << '\n';
    return 0;
}
}
