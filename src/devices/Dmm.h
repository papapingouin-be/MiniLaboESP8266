// Digital multimeter (DMM) device driver. This module uses the
// IORegistry to read raw analog values and converts them to user
// defined quantities according to the DMM configuration. Each DMM
// channel defines which IO source to use, how many decimals to
// display and optional threshold/hysteresis for binary modes. Only
// direct measurement (UDC) is implemented in this skeleton.

#ifndef MINILABOESP_DMM_H
#define MINILABOESP_DMM_H

#include <Arduino.h>
#include <ArduinoJson.h>

class IORegistry;
class Logger;
class ConfigStore;

class Dmm {
public:
  Dmm(IORegistry *ioReg, Logger *logger, ConfigStore *config);

  // Initialise the device by reading configuration. Must be called
  // after ConfigStore.begin().
  void begin();

  // Update internal state. Not used in this simple implementation but
  // kept for future expansions (filters, RMS calculations).
  void loop() {}

  // Produce a snapshot of all configured DMM channels. The returned
  // document contains an array named "channels" with objects:
  // { "id": <string>, "raw": <number>, "value": <float>, "unit": <string> }.
  void getSnapshot(JsonDocument &doc);

private:
  struct Channel {
    String ioId;
    String mode;
    uint8_t decimals;
    float threshold;
    float hyst;
  };
  static const size_t kMaxChannels = 8;
  Channel m_channels[kMaxChannels];
  size_t m_count;
  IORegistry *m_io;
  Logger *m_logger;
  ConfigStore *m_config;
};

#endif // MINILABOESP_DMM_H