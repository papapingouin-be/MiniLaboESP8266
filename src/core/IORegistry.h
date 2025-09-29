// IORegistry abstracts access to physical and virtual IO channels. It
// loads configuration from io.json and provides functions to read raw
// values from hardware (ADC channels), convert raw values to physical
// units using calibration coefficients, and expose virtual channels in
// the future (e.g. mathematical expressions or remote sources).

#ifndef MINILABOESP_IOREGISTRY_H
#define MINILABOESP_IOREGISTRY_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Adafruit_ADS1015.h>

class Logger;
class ConfigStore;

class IORegistry {
public:
  IORegistry(Logger *logger);
  ~IORegistry();

  // Initialise the registry by reading io.json. Should be called
  // during setup after ConfigStore.begin().
  void begin(ConfigStore *config);

  // Update any asynchronous sensors. For synchronous ADCs this is a
  // no-op but remote IO or polling-based sensors could be updated
  // here.
  void loop() {}

  // Read the raw integer value for the given channel identifier. If
  // the channel is unknown or unsupported this returns 0.
  int32_t readRaw(const String &id);

  // Convert a raw value to a physical value based on calibration
  // coefficients defined in io.json (k and b). If the channel is
  // unknown returns 0.0.
  float convert(const String &id, int32_t raw);

  // Convenience function to read and convert in one call.
  float readValue(const String &id);

private:
  struct Channel {
    String id;
    String type; // "a0", "ads1115", etc.
    uint8_t index;
    float k;
    float b;
  };

  // Maximum number of IO channels supported. Increase if you need more
  // channels but be mindful of memory usage.
  static const size_t kMaxChannels = 16;
  Channel m_channels[kMaxChannels];
  size_t m_channelCount;

  Logger *m_logger;
  ConfigStore *m_config;
  Adafruit_ADS1115 *m_ads;
  bool m_adsInitialized;
};

#endif // MINILABOESP_IOREGISTRY_H
