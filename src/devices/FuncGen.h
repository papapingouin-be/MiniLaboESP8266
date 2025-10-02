// Function generator device. Drives a DAC (MCP4725) to produce
// periodic waveforms such as sine, square, triangle or DC. The
// configuration is read from funcgen.json and can be updated at
// runtime via the web API. Frequency, amplitude and offset are
// specified as percentages of full scale.

#ifndef MINILABOESP_FUNCGEN_H
#define MINILABOESP_FUNCGEN_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Adafruit_MCP4725.h>

class Logger;
class ConfigStore;

class FuncGen {
public:
  enum Waveform { SINE, SQUARE, TRIANGLE, DC };

  FuncGen(Logger *logger, ConfigStore *config);

  // Initialise the DAC and load initial configuration.
  void begin();

  // Called in the main loop. Generates samples based on the current
  // waveform settings. Must be called regularly for accurate output.
  void loop();

  // Update settings from JSON document (e.g. via web API). The
  // document should contain keys: type ("sine", "square", "triangle",
  // "dc"),
  // freq (Hz), amp_pct (0–100), offset_pct (0–100), enabled (bool).
  void updateSettings(const JsonDocument &doc);

private:
  struct Settings {
    Waveform type;
    float freq;
    float amp;    // amplitude as fraction (0–1)
    float offset; // offset as fraction (0–1)
    bool enabled;
  } m_settings;

  Logger *m_logger;
  ConfigStore *m_config;
  Adafruit_MCP4725 m_dac;
  float m_phase;
  unsigned long m_lastMicros;
  bool m_disabledLogged;
  bool m_zeroFreqLogged;

  // Load settings from funcgen.json. Called during begin().
  void loadFromConfig();
  // Compute waveform sample at current phase.
  float waveformSample(float phase);
};

#endif // MINILABOESP_FUNCGEN_H
