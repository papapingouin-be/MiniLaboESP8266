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

  // Read the raw value for the given channel identifier. For local
  // analog inputs this corresponds to the ADC reading. For remote
  // channels received via UDP the cached network value is returned.
  // If the channel is unknown or unsupported this returns 0.
  float readRaw(const String &id);

  // Convert a raw value to a physical value based on calibration
  // coefficients defined in io.json (k and b). If the channel is
  // unknown returns 0.0.
  float convert(const String &id, float raw);

  // Convenience function to read and convert in one call.
  float readValue(const String &id);

  // Update the cached value of a remote UDP input. The value is matched
  // against the configured remote descriptors (MAC/IP/hostname and
  // channel identifier). Returns the number of channels updated.
  size_t updateRemoteValue(const String &mac, const String &ip,
                           const String &channelId,
                           const String &channelLabel, float raw,
                           float value, const String &unit,
                           const String &hostname);

  // Provide a description of the available IO hardware so the web UI
  // can expose the right options. The document is cleared and filled
  // with an object that lists available local inputs and their indexes.
  void describeHardware(JsonDocument &doc);

  // Produce a snapshot of all configured channels including the latest
  // raw reading, converted value and configured unit.
  void snapshot(JsonDocument &doc);

  // Describe the configured channels in a JSON array. Each entry contains
  // the identifier, type, index, calibration coefficients and unit. The
  // provided array is cleared before data is appended.
  void describeChannels(JsonArray &arr) const;

private:
  bool ensureAdsReady();

  struct RemoteInfo {
    RemoteInfo()
        : rxPort(0), txPort(0), channelIndex(0) {}
    String mac;
    String ip;
    String hostname;
    uint16_t rxPort;
    uint16_t txPort;
    String channelId;
    String channelLabel;
    String channelType;
    int channelIndex;
    String channelUnit;
  };

  struct Channel {
    String id;
    String type; // "a0", "ads1115", etc.
    uint8_t index;
    float k;
    float b;
    String unit;
    bool isUdpIn;
    bool hasRemote;
    RemoteInfo remote;
    String resolvedMac;
    String resolvedIp;
    String resolvedHostname;
    float lastRemoteRaw;
    float lastRemoteValue;
    bool remoteHasRaw;
    bool remoteHasValue;
    unsigned long remoteLastUpdate;
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
  bool m_adsAttempted;
};

#endif // MINILABOESP_IOREGISTRY_H
