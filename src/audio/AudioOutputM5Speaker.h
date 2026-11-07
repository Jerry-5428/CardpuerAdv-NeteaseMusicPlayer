// AudioOutputM5Speaker.h
//
// ESP8266Audio -> M5Unified Speaker bridge.
// Adapted from the M5Unified example "Advanced/MP3_with_ESP8266Audio"
// (MIT License, (c) M5Stack / LovyanGFX).  Kept as a local class so the
// decoder can run in its own FreeRTOS task while the UI never blocks.
#pragma once

#include <AudioOutput.h>
#include <M5Unified.h>

#include <atomic>

class AudioOutputM5Speaker : public AudioOutput {
 public:
  explicit AudioOutputM5Speaker(m5::Speaker_Class* m5sound, uint8_t virtual_sound_channel = 0) {
    _m5sound = m5sound;
    _virtual_ch = virtual_sound_channel;
  }
  ~AudioOutputM5Speaker() override {}

  // Call once after M5.begin().
  void setup() { _m5sound->setBufferReleaseCallback(this, bufferReleased); }

  bool begin() override { return true; }

  bool ConsumeSample(int16_t sample[2]) override {
    if (_buffer_index < buf_size) {
      _buffer[_index][_buffer_index] = sample[0];
      _buffer[_index][_buffer_index + 1] = sample[1];
      _buffer_index += 2;
      return true;
    }
    flush();
    return false;
  }

  void flush() override {
    if (_buffer_index) {
      _busy[_index] = true;
      const bool queued =
          _m5sound->playRaw(_buffer[_index], _buffer_index, hertz, true, 1, _virtual_ch);
      if (queued) {
        _frames.fetch_add(_buffer_index / 2);
      } else {
        _busy[_index] = false;
      }
      if (++_index >= buf_count) _index = 0;
      _buffer_index = 0;
      while (_busy[_index]) vTaskDelay(1);
    }
  }

  bool stop() override {
    flush();
    for (size_t i = 0; i < buf_count; ++i) {
      while (_busy[i]) vTaskDelay(1);
    }
    return true;
  }

  uint32_t getFrames() const { return _frames.load(); }
  uint32_t getRate() const { return hertz; }
  void resetFrames() { _frames.store(0); }

 private:
  m5::Speaker_Class* _m5sound;
  uint8_t _virtual_ch;
  static constexpr size_t buf_count = 3;
  static constexpr size_t buf_size = 1024;  // int16 entries (512 stereo frames)
  int16_t _buffer[buf_count][buf_size];
  std::atomic<bool> _busy[buf_count] = {{false}, {false}, {false}};
  size_t _buffer_index = 0;
  size_t _index = 0;
  std::atomic<uint32_t> _frames{0};

  static void bufferReleased(void* args, const void* data, uint8_t) {
    auto me = static_cast<AudioOutputM5Speaker*>(args);
    for (size_t i = 0; i < buf_count; ++i) {
      if (data == me->_buffer[i]) me->_busy[i] = false;
    }
  }
};
