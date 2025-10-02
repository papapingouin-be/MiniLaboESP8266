// Implementation of the function generator

#include "FuncGen.h"

#include "core/Logger.h"
#include "core/ConfigStore.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

namespace {
const char *waveformName(FuncGen::Waveform wave) {
  switch (wave) {
  case FuncGen::SINE:
    return "sine";
  case FuncGen::SQUARE:
    return "square";
  case FuncGen::TRIANGLE:
    return "triangle";
  case FuncGen::DC:
    return "dc";
  default:
    return "unknown";
  }
}
} // namespace

FuncGen::FuncGen(Logger *logger, ConfigStore *config)
    : m_logger(logger), m_config(config), m_phase(0.0f), m_lastMicros(0),
      m_disabledLogged(false), m_zeroFreqLogged(false),
      m_lastEnabledState(false), m_lastDcLevelLogged(-1.0f),
      m_lastOutputValue(-1.0f), m_lastLoggedOutput(-1.0f),
      m_noTargetLogged(false) {
  m_settings.type = SINE;
  m_settings.freq = 0.0f;
  m_settings.amp = 0.0f;
  m_settings.offset = 0.5f;
  m_settings.enabled = false;
  m_settings.targetId = F("DAC0");

  m_target.driver = DRIVER_NONE;
  m_target.id = F("");
  m_target.gpio = 0xFF;
  m_target.pwmFreq = 0;
  m_target.mcpAddress = 0x60;
  m_target.available = false;
}

void FuncGen::begin() {
  // Initialise the DAC with the default address. The actual address can
  // be overridden by the selected target configuration when
  // resolveTargetBinding() runs.
  m_dac.begin(0x60);
  // Load initial settings from funcgen.json
  loadFromConfig();
  resolveTargetBinding();
  // Set initial lastMicros to current time
  m_lastMicros = micros();
  m_disabledLogged = false;
  m_zeroFreqLogged = false;
  m_lastEnabledState = m_settings.enabled;
  m_lastDcLevelLogged = -1.0f;
  m_lastOutputValue = -1.0f;
  m_lastLoggedOutput = -1.0f;
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
  } else if (strcasecmp(typeStr, "dc") == 0) {
    m_settings.type = DC;
  }
  m_settings.freq = doc["freq"] | 0.0f;
  float amp_pct = doc["amp_pct"] | 0.0f;
  float offset_pct = doc["offset_pct"] | 50.0f;
  m_settings.amp = amp_pct / 100.0f;
  m_settings.offset = offset_pct / 100.0f;
  m_settings.enabled = doc["enabled"] | false;
  const char *target = doc["target"] | "DAC0";
  if (target) {
    m_settings.targetId = String(target);
  }
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
    } else if (strcasecmp(typeStr, "dc") == 0) {
      m_settings.type = DC;
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
  if (doc.containsKey("target")) {
    const char *target = doc["target"].as<const char *>();
    if (target && target[0]) {
      m_settings.targetId = String(target);
    }
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
    case DC:
      summary += F("dc");
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
    if (m_settings.targetId.length()) {
      summary += F(", target=");
      summary += m_settings.targetId;
    }
    m_logger->info(summary);

    if (old.enabled && !m_settings.enabled) {
      m_logger->warning(F("FuncGen disabled via update"));
    } else if (!old.enabled && m_settings.enabled) {
      m_logger->info(F("FuncGen enabled via update"));
    }
  }

  m_disabledLogged = false;
  m_zeroFreqLogged = false;
  if (!old.targetId.equalsIgnoreCase(m_settings.targetId)) {
    resolveTargetBinding();
  }
  // Persist settings to funcgen.json
  JsonDocument &cfg = m_config->getConfig("funcgen");
  const char *typeName = "sine";
  switch (m_settings.type) {
  case SINE:
    typeName = "sine";
    break;
  case SQUARE:
    typeName = "square";
    break;
  case TRIANGLE:
    typeName = "triangle";
    break;
  case DC:
    typeName = "dc";
    break;
  }
  cfg["type"] = typeName;
  cfg["freq"] = m_settings.freq;
  cfg["amp_pct"] = (int)(m_settings.amp * 100);
  cfg["offset_pct"] = (int)(m_settings.offset * 100);
  cfg["enabled"] = m_settings.enabled;
  if (m_settings.targetId.length()) {
    cfg["target"] = m_settings.targetId;
  } else {
    cfg.remove("target");
  }
  m_config->updateConfig("funcgen", cfg);
}

void FuncGen::snapshotStatus(JsonObject obj) const {
  if (!obj) {
    return;
  }

  const char *typeName = waveformName(m_settings.type);
  obj["type"] = typeName;
  obj["waveform"] = typeName;
  obj["freq"] = m_settings.freq;
  obj["amp_pct"] = (int)roundf(m_settings.amp * 100.0f);
  obj["offset_pct"] = (int)roundf(m_settings.offset * 100.0f);
  obj["amp_fraction"] = m_settings.amp;
  obj["offset_fraction"] = m_settings.offset;
  obj["enabled"] = m_settings.enabled;
  obj["timestamp_ms"] = (uint32_t)millis();
  if (m_settings.targetId.length()) {
    obj["target"] = m_settings.targetId;
  }

  JsonObject hw = obj["hardware"].isNull() ? obj.createNestedObject("hardware")
                                            : obj["hardware"].as<JsonObject>();
  const char *driverStr = "none";
  switch (m_target.driver) {
  case DRIVER_MCP4725:
    driverStr = "mcp4725";
    break;
  case DRIVER_PWM:
    driverStr = "pwm";
    break;
  case DRIVER_NONE:
  default:
    driverStr = "none";
    break;
  }
  hw["driver"] = driverStr;
  hw["available"] = m_target.available;
  if (m_target.id.length()) {
    hw["id"] = m_target.id;
  }
  if (m_target.driver == DRIVER_PWM) {
    hw["gpio"] = m_target.gpio;
    hw["pwm_freq"] = m_target.pwmFreq;
  } else if (m_target.driver == DRIVER_MCP4725) {
    char buf[8];
    snprintf(buf, sizeof(buf), "0x%02X", m_target.mcpAddress);
    hw["address"] = buf;
  }
  if (m_lastOutputValue >= 0.0f) {
    hw["last_output_fraction"] = m_lastOutputValue;
    hw["last_output_pct"] = m_lastOutputValue * 100.0f;
  }

  bool freqValid = m_settings.freq > 0.0f || m_settings.type == DC;
  obj["freq_valid"] = freqValid;

  String summary;
  summary.reserve(80);
  summary += m_settings.enabled ? F("Sortie active") : F("Sortie inactive");
  if (m_settings.targetId.length()) {
    summary += F(" (");
    summary += m_settings.targetId;
    summary += F(")");
  }
  if (m_target.available) {
    summary += F(" via ");
    summary += driverStr;
  } else {
    summary += F(" — cible indisponible");
  }
  obj["summary"] = summary;
  obj["message"] = summary;
}

void FuncGen::loop() {
  if (m_lastEnabledState != m_settings.enabled) {
    if (m_logger) {
      m_logger->info(String(F("FuncGen loop sees enabled=")) +
                     (m_settings.enabled ? F("true") : F("false")));
    }
    m_lastEnabledState = m_settings.enabled;
    if (!m_settings.enabled) {
      m_lastDcLevelLogged = -1.0f;
      ensureOutputDisabled();
    }
  }
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

  if (m_settings.type == DC) {
    m_zeroFreqLogged = false;
    float value = m_settings.amp;
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    if (m_logger) {
      if (m_lastDcLevelLogged < 0.0f ||
          fabsf(m_lastDcLevelLogged - value) >= 0.01f) {
        m_logger->info(String(F("FuncGen DC level => ")) +
                       String(value * 100.0f, 1) + F("% de l'échelle"));
        m_lastDcLevelLogged = value;
      }
    }
    writeOutput(value);
    return;
  }

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
  writeOutput(value);
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
  case DC:
    return 0.0f;
  default:
    return 0.0f;
  }
}

void FuncGen::resolveTargetBinding() {
  m_target.driver = DRIVER_NONE;
  m_target.available = false;
  m_target.id = m_settings.targetId;
  m_target.gpio = 0xFF;
  m_target.pwmFreq = 0;
  m_target.mcpAddress = 0x60;
  m_noTargetLogged = false;
  m_lastOutputValue = -1.0f;
  m_lastLoggedOutput = -1.0f;

  if (!m_config) {
    return;
  }

  JsonDocument &doc = m_config->getConfig("outputs");
  if (!doc.is<JsonArray>()) {
    if (m_logger) {
      m_logger->warning(F("FuncGen: outputs config is not an array"));
    }
    return;
  }

  JsonArray arr = doc.as<JsonArray>();
  for (JsonVariant v : arr) {
    if (!v.is<JsonObject>()) {
      continue;
    }
    JsonObject obj = v.as<JsonObject>();
    const char *id = obj["id"].as<const char *>();
    if (!id || !m_settings.targetId.equalsIgnoreCase(id)) {
      continue;
    }

    const char *type = obj["type"].as<const char *>();
    JsonObject cfg = obj["config"].is<JsonObject>()
                         ? obj["config"].as<JsonObject>()
                         : JsonObject();

    if (type && strcasecmp(type, "mcp4725") == 0) {
      uint8_t address = 0x60;
      if (cfg.containsKey("address")) {
        if (cfg["address"].is<const char *>()) {
          const char *addrStr = cfg["address"].as<const char *>();
          if (addrStr) {
            address = (uint8_t)strtol(addrStr, nullptr, 0);
          }
        } else if (cfg["address"].is<int>()) {
          address = (uint8_t)cfg["address"].as<int>();
        }
      }
      m_target.driver = DRIVER_MCP4725;
      m_target.available = true;
      m_target.mcpAddress = address;
      m_dac.begin(address);
      if (m_logger) {
        String addrStr = String(address, HEX);
        addrStr.toUpperCase();
        if (addrStr.length() < 2) {
          addrStr = "0" + addrStr;
        }
        m_logger->info(String(F("FuncGen target MCP4725 @0x")) + addrStr);
      }
      return;
    }

    if (type && (strcasecmp(type, "pwm_rc") == 0 ||
                 strcasecmp(type, "pwm_0_10v") == 0 ||
                 strcasecmp(type, "charge_pump_doubler") == 0)) {
      String pinLabel;
      if (cfg["pin"].is<const char *>()) {
        const char *pinStr = cfg["pin"].as<const char *>();
        if (pinStr) pinLabel = String(pinStr);
      } else if (cfg["pin"].is<int>()) {
        pinLabel = String(cfg["pin"].as<int>());
      }
      int gpio = labelToGpio(pinLabel);
      if (gpio < 0) {
        if (m_logger) {
          m_logger->warning(String(F("FuncGen: invalid GPIO for target ")) +
                            m_settings.targetId);
        }
        return;
      }
      uint32_t freq = 0;
      if (cfg["frequency"].is<uint32_t>()) {
        freq = cfg["frequency"].as<uint32_t>();
      } else if (cfg["frequency"].is<float>()) {
        freq = (uint32_t)(cfg["frequency"].as<float>() + 0.5f);
      } else if (cfg["pwm"].is<JsonObject>()) {
        JsonObject pwmObj = cfg["pwm"].as<JsonObject>();
        if (pwmObj["frequency"].is<uint32_t>()) {
          freq = pwmObj["frequency"].as<uint32_t>();
        } else if (pwmObj["frequency"].is<float>()) {
          freq = (uint32_t)(pwmObj["frequency"].as<float>() + 0.5f);
        }
      }
      if (freq == 0) {
        if (strcasecmp(type, "pwm_rc") == 0) {
          freq = 5000;
        } else if (strcasecmp(type, "pwm_0_10v") == 0) {
          freq = 2000;
        } else {
          freq = 4000;
        }
      }

      pinMode(gpio, OUTPUT);
      analogWriteRange(1023);
      analogWriteFreq(freq);
      analogWrite(gpio, 0);

      m_target.driver = DRIVER_PWM;
      m_target.available = true;
      m_target.gpio = (uint8_t)gpio;
      m_target.pwmFreq = freq;
      if (m_logger) {
        m_logger->info(String(F("FuncGen target PWM sur GPIO")) +
                       String(gpio) + F(" @") + String(freq) + F("Hz"));
      }
      return;
    }

    if (m_logger) {
      m_logger->warning(String(F("FuncGen: unsupported target type ")) +
                        (type ? type : "?"));
    }
    return;
  }

  if (m_logger) {
    m_logger->warning(String(F("FuncGen: cible introuvable ")) +
                      m_settings.targetId);
  }
}

int FuncGen::labelToGpio(const String &label) {
  if (!label.length()) {
    return -1;
  }
  String trimmed = label;
  trimmed.trim();
  if (!trimmed.length()) {
    return -1;
  }

  struct PinMapEntry {
    const char *label;
    uint8_t gpio;
  };
  static const PinMapEntry map[] = {{"D0", 16}, {"D1", 5},  {"D2", 4},
                                    {"D3", 0},  {"D4", 2},  {"D5", 14},
                                    {"D6", 12}, {"D7", 13}, {"D8", 15}};
  for (const auto &entry : map) {
    if (trimmed.equalsIgnoreCase(entry.label)) {
      return entry.gpio;
    }
  }

  String upper = trimmed;
  upper.toUpperCase();
  if (upper.startsWith("GPIO")) {
    String numeric = upper.substring(4);
    if (numeric.length()) {
      return numeric.toInt();
    }
  }

  bool digits = true;
  for (size_t i = 0; i < trimmed.length(); ++i) {
    char c = trimmed.charAt(i);
    if (c < '0' || c > '9') {
      digits = false;
      break;
    }
  }
  if (digits) {
    return trimmed.toInt();
  }
  return -1;
}

void FuncGen::writeOutput(float value) {
  if (!m_target.available || m_target.driver == DRIVER_NONE) {
    if (m_logger && !m_noTargetLogged) {
      m_logger->warning(String(F("FuncGen: aucune sortie active (")) +
                        (m_settings.targetId.length() ? m_settings.targetId
                                                       : String("")) +
                        F(")"));
      m_noTargetLogged = true;
    }
    return;
  }
  m_noTargetLogged = false;

  if (value < 0.0f) value = 0.0f;
  if (value > 1.0f) value = 1.0f;

  if (m_lastOutputValue >= 0.0f && fabsf(m_lastOutputValue - value) < 0.0005f) {
    return;
  }

  bool shouldLog = false;
  if (m_logger) {
    if (m_lastLoggedOutput < 0.0f ||
        fabsf(m_lastLoggedOutput - value) >= 0.05f) {
      shouldLog = true;
    }
  }

  switch (m_target.driver) {
  case DRIVER_MCP4725: {
    uint16_t dacVal = (uint16_t)(value * 4095.0f + 0.5f);
    m_dac.setVoltage(dacVal, false);
    break;
  }
  case DRIVER_PWM: {
    uint16_t pwmVal = (uint16_t)(value * 1023.0f + 0.5f);
    analogWrite(m_target.gpio, pwmVal);
    break;
  }
  case DRIVER_NONE:
  default:
    break;
  }

  m_lastOutputValue = value;
  if (shouldLog && m_logger) {
    String msg = String(F("FuncGen sortie -> "));
    msg += String(value * 100.0f, 1);
    msg += F("% (driver=");
    switch (m_target.driver) {
    case DRIVER_MCP4725: {
      msg += F("mcp4725 @0x");
      String addr = String(m_target.mcpAddress, HEX);
      addr.toUpperCase();
      if (addr.length() < 2) {
        addr = "0" + addr;
      }
      msg += addr;
      break;
    }
    case DRIVER_PWM:
      msg += F("pwm,gpio=");
      msg += String(m_target.gpio);
      msg += F(",freq=");
      msg += String(m_target.pwmFreq);
      break;
    case DRIVER_NONE:
    default:
      msg += F("none");
      break;
    }
    msg += F(")");
    m_logger->debug(msg);
    m_lastLoggedOutput = value;
  }
}

void FuncGen::ensureOutputDisabled() {
  if (!m_target.available) {
    return;
  }
  if (m_lastOutputValue >= 0.0f && fabsf(m_lastOutputValue) < 0.0005f) {
    return;
  }
  switch (m_target.driver) {
  case DRIVER_MCP4725:
    m_dac.setVoltage(0, false);
    break;
  case DRIVER_PWM:
    analogWrite(m_target.gpio, 0);
    break;
  case DRIVER_NONE:
  default:
    break;
  }
  m_lastOutputValue = 0.0f;
  if (m_logger) {
    m_logger->info(F("FuncGen sortie désactivée (niveau 0)"));
  }
  m_lastLoggedOutput = 0.0f;
}
