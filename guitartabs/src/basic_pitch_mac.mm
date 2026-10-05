#include "basic_pitch.hpp"
#include "model_embed.hpp"

#import <CoreML/CoreML.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace guitartabs {
namespace {

constexpr int kRate = 22050;
constexpr int kWindow = 43844;
constexpr int kModelFrames = 172;
constexpr int kBins = 88;
constexpr int kHop = 256;
constexpr int kOverlapFrames = 30;
constexpr int kHalfOverlap = kOverlapFrames / 2;
constexpr int kPad = kHalfOverlap * kHop;
constexpr int kStep = kWindow - kOverlapFrames * kHop;

const char kBasicPitchNotice[] = "Basic Pitch (c) 2022 Spotify AB, Apache License 2.0";

__strong MLModel* gPitchModel = nil;

std::string& loadError() {
  static std::string error;
  return error;
}

MLModel* pitchModel() { return gPitchModel; }

void setPitchModel(MLModel* model) { gPitchModel = model; }

std::once_flag& loadOnce() {
  static std::once_flag once;
  return once;
}

NSString* cacheDirectory() {
  NSArray<NSString*>* paths = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
  NSString* base = paths.count > 0 ? paths[0] : NSTemporaryDirectory();
  return [base stringByAppendingPathComponent:@"Dagniele/GuitarTabs"];
}

void writePackage(NSString* package) {
  NSFileManager* files = [NSFileManager defaultManager];
  NSError* error = nil;
  if (![files createDirectoryAtPath:package withIntermediateDirectories:YES attributes:nil error:&error]) {
    throw std::runtime_error(error.localizedDescription.UTF8String);
  }
  for (int index = 0; index < kBasicPitchFileCount; ++index) {
    const ModelFile& file = kBasicPitchFiles[index];
    NSString* path = [package stringByAppendingPathComponent:[NSString stringWithUTF8String:file.path]];
    NSString* parent = path.stringByDeletingLastPathComponent;
    if (![files createDirectoryAtPath:parent withIntermediateDirectories:YES attributes:nil error:&error]) {
      throw std::runtime_error(error.localizedDescription.UTF8String);
    }
    NSData* data = [NSData dataWithBytes:file.data length:file.size];
    if (![data writeToFile:path options:NSDataWritingAtomic error:&error]) {
      throw std::runtime_error(error.localizedDescription.UTF8String);
    }
  }
}

void ensureModel() {
  std::call_once(loadOnce(), [] {
    @autoreleasepool {
      try {
        (void)kBasicPitchNotice[0];
        if (kBasicPitchFileCount <= 0) throw std::runtime_error("Guitar Tabs was built without its pitch model");
        NSString* cache = cacheDirectory();
        NSString* package = [cache stringByAppendingPathComponent:@"nmp.mlpackage"];
        NSString* compiled = [cache stringByAppendingPathComponent:@"nmp.mlmodelc"];
        NSFileManager* files = [NSFileManager defaultManager];
        writePackage(package);
        NSError* error = nil;
        if (![files fileExistsAtPath:compiled]) {
          NSURL* temporary = [MLModel compileModelAtURL:[NSURL fileURLWithPath:package] error:&error];
          if (!temporary) throw std::runtime_error(error.localizedDescription.UTF8String);
          [files removeItemAtPath:compiled error:nil];
          if (![files moveItemAtURL:temporary toURL:[NSURL fileURLWithPath:compiled] error:&error]) {
            throw std::runtime_error(error.localizedDescription.UTF8String);
          }
        }
        MLModelConfiguration* config = [MLModelConfiguration new];
        config.computeUnits = MLComputeUnitsCPUOnly;
        MLModel* model = [MLModel modelWithContentsOfURL:[NSURL fileURLWithPath:compiled] configuration:config error:&error];
        if (!model) throw std::runtime_error(error.localizedDescription.UTF8String);
        setPitchModel(model);
      } catch (const std::exception& ex) {
        loadError() = ex.what();
      }
    }
  });
  if (!pitchModel()) throw std::runtime_error(loadError().empty() ? "Basic Pitch failed to load" : loadError());
}

std::vector<float> resample(const float* samples, int count, int sampleRate, int& outCount) {
  if (sampleRate == kRate) {
    outCount = count;
    return std::vector<float>(samples, samples + count);
  }
  const double ratio = static_cast<double>(kRate) / static_cast<double>(sampleRate);
  outCount = std::max(1, static_cast<int>(std::llround(static_cast<double>(count) * ratio)));
  std::vector<float> out(static_cast<size_t>(outCount));
  const int last = count - 1;
  for (int i = 0; i < outCount; ++i) {
    const double source = static_cast<double>(i) / ratio;
    const int left = std::clamp(static_cast<int>(std::floor(source)), 0, last);
    const int right = std::min(last, left + 1);
    const float frac = static_cast<float>(source - left);
    out[static_cast<size_t>(i)] = samples[left] * (1.f - frac) + samples[right] * frac;
  }
  return out;
}

void readOutput(MLMultiArray* array, int time, float* destination) {
  const float* data = static_cast<const float*>(array.dataPointer);
  const NSInteger timeStride = array.strides[1].integerValue;
  const NSInteger freqStride = array.strides[2].integerValue;
  for (int freq = 0; freq < kBins; ++freq) destination[freq] = data[time * timeStride + freq * freqStride];
}

std::mutex& predictMutex() {
  static std::mutex mutex;
  return mutex;
}

}  // namespace

std::vector<PitchNote> analyzePitch(const float* samples, int count, int sampleRate, int minMidi, int maxMidi,
                                    std::atomic<float>* progress) {
  if (!samples || count < kHop * 4 || sampleRate < 1000) return {};
  float peak = 0.f;
  for (int i = 0; i < count; ++i) {
    if (!std::isfinite(samples[i])) throw std::runtime_error("Track audio contains invalid samples");
    peak = std::max(peak, std::abs(samples[i]));
  }
  if (peak < 1e-5f) return {};
  ensureModel();

  int usedCount = 0;
  std::vector<float> audio = resample(samples, count, sampleRate, usedCount);
  const float gain = peak > 1.f ? 0.99f / peak : 1.f;
  if (gain != 1.f) {
    for (float& sample : audio) sample *= gain;
  }

  std::vector<float> padded(static_cast<size_t>(kPad + usedCount), 0.f);
  std::copy(audio.begin(), audio.end(), padded.begin() + kPad);

  std::vector<float> note;
  std::vector<float> onset;
  std::vector<double> times;
  int windowIndex = 0;
  int windowCount = 0;
  for (int start = 0; start < static_cast<int>(padded.size()); start += kStep) ++windowCount;
  windowCount = std::max(1, windowCount);

  std::lock_guard<std::mutex> lock(predictMutex());
  for (int start = 0; start < static_cast<int>(padded.size()); start += kStep) {
    if (progress) progress->store(static_cast<float>(windowIndex) / static_cast<float>(windowCount), std::memory_order_relaxed);
    @autoreleasepool {
      NSError* error = nil;
      MLMultiArray* input = [[MLMultiArray alloc] initWithShape:@[@1, @(kWindow), @1] dataType:MLMultiArrayDataTypeFloat32 error:&error];
      if (!input) throw std::runtime_error(error.localizedDescription.UTF8String);
      float* destination = static_cast<float*>(input.dataPointer);
      const NSInteger stride = input.strides[1].integerValue;
      for (int i = 0; i < kWindow; ++i) {
        const int source = start + i;
        destination[i * stride] = source < static_cast<int>(padded.size()) ? padded[static_cast<size_t>(source)] : 0.f;
      }
      MLDictionaryFeatureProvider* provider =
          [[MLDictionaryFeatureProvider alloc] initWithDictionary:@{@"input_2" : [MLFeatureValue featureValueWithMultiArray:input]}
                                                            error:&error];
      if (!provider) throw std::runtime_error(error.localizedDescription.UTF8String);
      id<MLFeatureProvider> output = [pitchModel() predictionFromFeatures:provider error:&error];
      if (!output) throw std::runtime_error(error.localizedDescription.UTF8String);
      MLMultiArray* noteArray = [output featureValueForName:@"Identity_1"].multiArrayValue;
      MLMultiArray* onsetArray = [output featureValueForName:@"Identity_2"].multiArrayValue;
      if (!noteArray || !onsetArray) throw std::runtime_error("Basic Pitch returned an unexpected output");
      float noteFrame[kBins];
      float onsetFrame[kBins];
      for (int frame = kHalfOverlap; frame < kModelFrames - kHalfOverlap; ++frame) {
        const int realSample = start + frame * kHop - kPad;
        if (realSample < 0 || realSample >= usedCount) continue;
        readOutput(noteArray, frame, noteFrame);
        readOutput(onsetArray, frame, onsetFrame);
        note.insert(note.end(), noteFrame, noteFrame + kBins);
        onset.insert(onset.end(), onsetFrame, onsetFrame + kBins);
        times.push_back(static_cast<double>(realSample) / static_cast<double>(kRate));
      }
    }
    ++windowIndex;
  }

  if (progress) progress->store(1.f, std::memory_order_relaxed);
  const int frames = static_cast<int>(times.size());
  const int minFrames = std::max(4, static_cast<int>(std::lround(0.07 * static_cast<double>(kRate) / static_cast<double>(kHop))));
  return decodePitch(note.data(), onset.data(), times.data(), frames, minMidi, maxMidi, 0.5f, 0.3f, minFrames);
}

}  // namespace guitartabs
