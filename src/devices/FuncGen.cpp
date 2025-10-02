// Implementation of the function generator

#include "FuncGen.h"

#include "core/Logger.h"
#include "core/ConfigStore.h"

FuncGen::FuncGen(Logger *logger, ConfigStore *config)
    : m_settings{SINE, 0.0f, 0.0f, 0.5f, false}, m_logger(logger),
      m_config(config), m_phase(0.0f), m_lastMicros(0),
      m_disabledLogged(false), m_zeroFreqLogged(false) {}

void FuncGen::begin() {
  // Initialise the DAC. It defaults to address 0x60. If the device is
  // missing or not connected begin() will return false. We ignore
  // failure because the user may not have connected a DAC.
  m_dac.begin(0x60);
  // Load initial settings from funcgen.json
  loadFromConfig();
  // Set initial lastMicros to current time
  m_lastMicros = micros();
  m_disabledLogged = false;
  m_zeroFreqLogged = false;
}

void FuncGen::loadFromConfig() {
  JsonDocument &doc = m_config->getConfig("funcgen");
  if (!doc.is<JsonObject>()) {
    return;
  }
  const char *typeStr = doc["type"] | "sine";
  if (strcasecmp(typeStr, "sine") == 0) {
    m_settings.type = SINE;
  } else if (strcasecmp(typeStr, "square") == 0) {
    m_settings.type = SQUARE;
  } else if (strcasecmp(typeStr, "triangle") == 0) {
    m_settings.type = TRIANGLE;
  }
  m_settings.freq = doc["freq"] | 0.0f;
  float amp_pct = doc["amp_pct"] | 0.0f;
  float offset_pct = doc["offset_pct"] | 50.0f;
  m_settings.amp = amp_pct / 100.0f;
  m_settings.offset = offset_pct / 100.0f;
  m_settings.enabled = doc["enabled"] | false;
}

void FuncGen::updateSettings(const JsonDocument &doc) {
  // Update internal settings from the provided document. Do minimal
  // validation to ensure values stay within [0,1].
  if (m_logger) {
    String payload;
    serializeJson(doc, payload);
    m_logger->info(String(F("FuncGen updateSettings payload=")) + payload);
  }

  Settings old = m_settings;
  if (doc.containsKey("type")) {
    const char *typeStr = doc["type"].as<const char *>();
    if (strcasecmp(typeStr, "sine") == 0) {
      m_settings.type = SINE;
    } else if (strcasecmp(typeStr, "square") == 0) {
      m_settings.type = SQUARE;
    } else if (strcasecmp(typeStr, "triangle") == 0) {
      m_settings.type = TRIANGLE;
    }
  }
  if (doc.containsKey("freq")) {
    m_settings.freq = doc["freq"];
  }
  if (doc.containsKey("amp_pct")) {
    float pct = doc["amp_pct"];
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    m_settings.amp = pct / 100.0f;
  }
  if (doc.containsKey("offset_pct")) {
    float pct = doc["offset_pct"];
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    m_settings.offset = pct / 100.0f;
  }
  if (doc.containsKey("enabled")) {
    m_settings.enabled = doc["enabled"];
  }

  if (m_logger) {
    String summary = String(F("FuncGen settings => type="));
    switch (m_settings.type) {
    case SINE:
      summary += F("sine");
      break;
    case SQUARE:
      summary += F("square");
      break;
    case TRIANGLE:
      summary += F("triangle");
      break;
    }
    summary += F(", freq=");
    summary += String(m_settings.freq, 3);
    summary += F("Hz, amp=");
    summary += String(m_settings.amp, 3);
    summary += F(", offset=");
    summary += String(m_settings.offset, 3);
    summary += F(", enabled=");
    summary += (m_settings.enabled ? F("true") : F("false"));
    m_logger->info(summary);

    if (old.enabled && !m_settings.enabled) {
      m_logger->warning(F("FuncGen disabled via update"));
    } else if (!old.enabled && m_settings.enabled) {
      m_logger->info(F("FuncGen enabled via update"));
    }
  }

  m_disabledLogged = false;
  m_zeroFreqLogged = false;
  // Persist settings to funcgen.json
  JsonDocument &cfg = m_config->getConfig("funcgen");
  cfg["type"] = (m_settings.type == SINE
                      ? "sine"
                      : (m_settings.type == SQUARE ? "square" : "triangle"));
  cfg["freq"] = m_settings.freq;
  cfg["amp_pct"] = (int)(m_settings.amp * 100);
  cfg["offset_pct"] = (int)(m_settings.offset * 100);
  cfg["enabled"] = m_settings.enabled;
  m_config->updateConfig("funcgen", cfg);
}

void FuncGen::loop() {
  if (!m_settings.enabled) {
    if (m_logger && !m_disabledLogged) {
      m_logger->debug(F("FuncGen loop skipped: generator disabled"));
      m_disabledLogged = true;
    }
    return;
  }
  m_disabledLogged = false;
  // Compute elapsed time since last call
  unsigned long now = micros();
  unsigned long delta = now - m_lastMicros;
  m_lastMicros = now;
  // If frequency is zero nothing to generate
  if (m_settings.freq <= 0.0f) {
    if (m_logger && !m_zeroFreqLogged) {
      m_logger->warning(F("FuncGen loop skipped: frequency <= 0"));
      m_zeroFreqLogged = true;
    }
    return;
  }
  m_zeroFreqLogged = false;
  // Update phase based on elapsed microseconds. Phase wraps around
  // 0..1.
  float inc = (float)delta * m_settings.freq / 1000000.0f;
  m_phase += inc;
  if (m_phase >= 1.0f) m_phase -= 1.0f;
  // Compute waveform sample in range [0,1]
  float sample = waveformSample(m_phase);
  // Apply amplitude and offset: final = offset + amp * sample
  float value = m_settings.offset + m_settings.amp * sample;
  // Clamp to [0,1]
  if (value < 0.0f) value = 0.0f;
  if (value > 1.0f) value = 1.0f;
  // Convert to 12-bit value for DAC
  uint16_t dacVal = (uint16_t)(value * 4095.0f + 0.5f);
  m_dac.setVoltage(dacVal, false);
}

float FuncGen::waveformSample(float phase) {
  switch (m_settings.type) {
  case SINE:
    return 0.5f * (1.0f + sinf(2.0f * PI * phase));
  case SQUARE:
    return phase < 0.5f ? 1.0f : -1.0f;
  case TRIANGLE:
    if (phase < 0.5f) {
      return 4.0f * phase - 1.0f; // rises from -1 to 1
    } else {
      return 3.0f - 4.0f * phase; // falls from 1 to -1
    }
  default:
    return 0.0f;
  }
}