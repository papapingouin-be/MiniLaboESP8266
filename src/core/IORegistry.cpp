// Implementation of the IORegistry class

#include "IORegistry.h"

#include <type_traits>

#include "Logger.h"
#include "ConfigStore.h"

namespace {
template <typename T>
bool beginAdsDevice(T *ads, std::true_type) {
  return ads->begin();
}

template <typename T>
bool beginAdsDevice(T *ads, std::false_type) {
  ads->begin();
  return true;
}

template <typename T>
bool beginAdsDevice(T *ads) {
  using ReturnType = decltype(ads->begin());
  return beginAdsDevice(ads, std::is_same<ReturnType, bool>{});
}
} // namespace

IORegistry::IORegistry(Logger *logger)
    : m_channelCount(0), m_logger(logger), m_config(nullptr), m_ads(nullptr),
      m_adsInitialized(false), m_adsAttempted(false) {}

IORegistry::~IORegistry() {
  if (m_ads) {
    delete m_ads;
    m_ads = nullptr;
  }
}

void IORegistry::begin(ConfigStore *config) {
  m_config = config;
  m_channelCount = 0;

  if (!m_config) {
    if (m_logger)
      m_logger->error("IORegistry.begin called without ConfigStore");
    return;
  }

  // Read configuration from io.json. The expected format is an array of
  // objects, each with at least an "id" and "type" field. Optional
  // fields include "index" (ADC channel number) and calibration
  // coefficients "k" and "b". If the file is missing or empty no
  // channels will be configured.
  JsonDocument &doc = m_config->getConfig("io");
  JsonArray arr;
  if (doc.is<JsonArray>()) {
    arr = doc.as<JsonArray>();
  } else if (doc.is<JsonObject>()) {
    JsonObject obj = doc.as<JsonObject>();
    if (obj.containsKey("channels")) {
      arr = obj["channels"].as<JsonArray>();
    }
  }
  if (arr.isNull()) {
    if (m_logger)
      m_logger->warning("io.json missing or invalid; no IO channels configured");
    Serial.println(F("[WARN] io.json missing/invalid, no IO channels loaded"));
    return;
  }

  bool hasAnalogInput = false;
  for (JsonVariant v : arr) {
    if (m_channelCount >= kMaxChannels)
      break;
    if (!v.is<JsonObject>())
      continue;
    JsonObject obj = v.as<JsonObject>();
    Channel &ch = m_channels[m_channelCount++];
    ch.id = obj["id"] | String("ch") + String(m_channelCount);
    ch.id.trim();
    ch.type = obj["type"] | "a0";
    ch.type.toLowerCase();
    ch.index = obj["index"] | 0;
    ch.k = obj["k"] | 1.0;
    ch.b = obj["b"] | 0.0;
    const char *unit = obj["unit"] | "V";
    ch.unit = unit;
    if (ch.type == "a0") {
      hasAnalogInput = true;
    }
    if (m_logger) {
      String msg = String("IO channel ") + ch.id + " type=" + ch.type +
                   " index=" + String(ch.index);
      m_logger->info(msg);
    }
    Serial.println(String(F("[IO] Channel loaded: id=")) + ch.id +
                   F(" type=") + ch.type + F(" index=") + String(ch.index));
  }

  if (m_logger) {
    m_logger->info(String("Configured ") + String(m_channelCount) +
                   String(" IO channel(s)"));
  }
  Serial.println(String(F("[IO] Total channels configured: ")) +
                 String(m_channelCount));

  if (hasAnalogInput) {
    pinMode(A0, INPUT);
    Serial.println(F("[IO] Configured A0 as analog input"));
  }

  // Determine if ADS1115 is required
  bool needAds = false;
  for (size_t i = 0; i < m_channelCount; i++) {
    if (m_channels[i].type == "ads1115") {
      needAds = true;
      break;
    }
  }
  if (needAds) {
    ensureAdsReady();
  }
}

int32_t IORegistry::readRaw(const String &id) {
  // Search for channel by id
  for (size_t i = 0; i < m_channelCount; i++) {
    if (m_channels[i].id == id) {
      const Channel &ch = m_channels[i];
      if (ch.type == "a0") {
        // Read the built-in ADC. The ESP8266 ADC returns 0-1023.
        int raw = analogRead(A0);
        return raw;
      } else if (ch.type == "ads1115") {
        if (!m_adsInitialized) {
          ensureAdsReady();
        }
        if (m_adsInitialized && ch.index < 4) {
          int16_t val = m_ads->readADC_SingleEnded(ch.index);
          return (int32_t)val;
        }
        return 0;
      } else {
        // Unknown type; return zero
        return 0;
      }
    }
  }
  // If not found return zero
  return 0;
}

float IORegistry::convert(const String &id, int32_t raw) {
  for (size_t i = 0; i < m_channelCount; i++) {
    if (m_channels[i].id == id) {
      return m_channels[i].k * raw + m_channels[i].b;
    }
  }
  return 0.0f;
}

float IORegistry::readValue(const String &id) {
  int32_t raw = readRaw(id);
  return convert(id, raw);
}

bool IORegistry::ensureAdsReady() {
  if (!m_ads) {
    m_ads = new Adafruit_ADS1115();
    m_adsInitialized = false;
    m_adsAttempted = false;
  }
  if (m_adsAttempted) {
    return m_adsInitialized;
  }
  m_adsAttempted = true;
  if (beginAdsDevice(m_ads)) {
    // Use the default gain of +/-4.096V (1 bit = 0.125mV)
    m_ads->setGain(GAIN_ONE);
    m_adsInitialized = true;
    if (m_logger) m_logger->info("ADS1115 initialized");
  } else {
    m_adsInitialized = false;
    if (m_logger) m_logger->warning("ADS1115 init failed");
  }
  return m_adsInitialized;
}

void IORegistry::describeHardware(JsonDocument &doc) {
  doc.clear();
  JsonArray locals = doc.createNestedArray("localInputs");

  JsonObject a0 = locals.createNestedObject();
  a0["type"] = "a0";
  a0["label"] = "ADC interne A0";
  a0["defaultId"] = "A0";
  a0["defaultUnit"] = "V";
  a0["available"] = true;
  JsonArray a0Indexes = a0.createNestedArray("indexes");
  JsonObject a0Index = a0Indexes.createNestedObject();
  a0Index["value"] = 0;
  a0Index["label"] = "A0";

  bool adsAvailable = ensureAdsReady();
  JsonObject ads = locals.createNestedObject();
  ads["type"] = "ads1115";
  ads["label"] = "ADS1115";
  ads["defaultId"] = "ADS";
  ads["defaultUnit"] = "V";
  ads["available"] = adsAvailable;
  JsonArray adsIndexes = ads.createNestedArray("indexes");
  const char *labels[] = {"A0", "A1", "A2", "A3"};
  for (uint8_t i = 0; i < 4; i++) {
    JsonObject idx = adsIndexes.createNestedObject();
    idx["value"] = i;
    idx["label"] = labels[i];
  }

  JsonArray outputs = doc.createNestedArray("localOutputs");

  struct PwmPinInfo {
    const char *value;
    const char *label;
    uint8_t gpio;
  };
  static const PwmPinInfo kPwmPins[] = {
      {"D1", "D1 (GPIO5)", 5},  {"D2", "D2 (GPIO4)", 4},
      {"D5", "D5 (GPIO14)", 14}, {"D6", "D6 (GPIO12)", 12},
      {"D7", "D7 (GPIO13)", 13}, {"D8", "D8 (GPIO15)", 15},
  };

  JsonObject pwmRc = outputs.createNestedObject();
  pwmRc["type"] = "pwm_rc";
  pwmRc["label"] = "PWM filtrée (RC)";
  pwmRc["defaultId"] = "AO0";
  pwmRc["defaultUnit"] = "V";
  pwmRc["summary"] =
      "Sortie PWM 1–40 kHz filtrée par RC (R=10 kΩ, C=10 µF typiques)";
  JsonObject pwmRcRange = pwmRc.createNestedObject("range");
  pwmRcRange["min"] = 0.0f;
  pwmRcRange["max"] = 3.3f;
  pwmRcRange["unit"] = "V";
  JsonObject pwmRcFilter = pwmRc.createNestedObject("filter");
  pwmRcFilter["r_ohm"] = 10000;
  pwmRcFilter["c_uF"] = 10;
  JsonObject pwmRcFrequency = pwmRc.createNestedObject("frequency");
  pwmRcFrequency["min"] = 1000;
  pwmRcFrequency["max"] = 40000;
  pwmRcFrequency["default"] = 5000;
  JsonArray pwmRcModes = pwmRc.createNestedArray("pwmModes");
  {
    JsonObject mode = pwmRcModes.createNestedObject();
    mode["id"] = "balanced";
    mode["label"] = "Équilibré (≈1 kHz)";
    mode["frequency"] = 1000;
  }
  {
    JsonObject mode = pwmRcModes.createNestedObject();
    mode["id"] = "standard";
    mode["label"] = "Standard (≈5 kHz)";
    mode["frequency"] = 5000;
  }
  {
    JsonObject mode = pwmRcModes.createNestedObject();
    mode["id"] = "fast";
    mode["label"] = "Rapide (≈20 kHz)";
    mode["frequency"] = 20000;
  }
  JsonArray pwmRcPins = pwmRc.createNestedArray("pins");
  for (const auto &pin : kPwmPins) {
    JsonObject pinObj = pwmRcPins.createNestedObject();
    pinObj["value"] = pin.value;
    pinObj["label"] = pin.label;
    pinObj["gpio"] = pin.gpio;
  }
  JsonObject pwmRcTemplate = pwmRc.createNestedObject("configTemplate");
  pwmRcTemplate["pin"] = "D2";
  pwmRcTemplate["pwmMode"] = "balanced";
  pwmRcTemplate["frequency"] = 5000;
  JsonObject pwmRcTemplateFilter = pwmRcTemplate.createNestedObject("filter");
  pwmRcTemplateFilter["r_ohm"] = 10000;
  pwmRcTemplateFilter["c_uF"] = 10;
  JsonObject pwmRcTemplateRange =
      pwmRcTemplate.createNestedObject("range");
  pwmRcTemplateRange["min"] = 0.0f;
  pwmRcTemplateRange["max"] = 3.3f;
  pwmRcTemplateRange["unit"] = "V";
  pwmRcTemplate["notes"] =
      "Utiliser un filtre RC (10 kΩ / 10 µF) pour lisser la PWM.";

  JsonObject mcp = outputs.createNestedObject();
  mcp["type"] = "mcp4725";
  mcp["label"] = "MCP4725 (DAC 12 bits)";
  mcp["defaultId"] = "DAC0";
  mcp["defaultUnit"] = "V";
  mcp["summary"] = "DAC I²C 12 bits, sortie 0–3,3 V proportionnelle";
  JsonObject mcpRange = mcp.createNestedObject("range");
  mcpRange["min"] = 0.0f;
  mcpRange["max"] = 3.3f;
  mcpRange["unit"] = "V";
  JsonArray mcpAddresses = mcp.createNestedArray("addresses");
  mcpAddresses.add("0x60");
  mcpAddresses.add("0x61");
  JsonObject mcpTemplate = mcp.createNestedObject("configTemplate");
  mcpTemplate["address"] = "0x60";
  JsonObject mcpTemplateRange = mcpTemplate.createNestedObject("range");
  mcpTemplateRange["min"] = 0.0f;
  mcpTemplateRange["max"] = 3.3f;
  mcpTemplateRange["unit"] = "V";
  mcpTemplate["vref"] = 3.3f;
  mcpTemplate["notes"] =
      "Le MCP4725 utilise l’alimentation comme référence de tension.";

  JsonObject pwm10 = outputs.createNestedObject();
  pwm10["type"] = "pwm_0_10v";
  pwm10["label"] = "Convertisseur PWM → 0-10 V";
  pwm10["defaultId"] = "AO10";
  pwm10["defaultUnit"] = "V";
  pwm10["summary"] =
      "Module 12-30 V convertissant 0-100 % PWM en 0-10 V (±5 %)";
  JsonObject pwm10Range = pwm10.createNestedObject("range");
  pwm10Range["min"] = 0.0f;
  pwm10Range["max"] = 10.0f;
  pwm10Range["unit"] = "V";
  JsonObject pwm10Supply = pwm10.createNestedObject("supply");
  pwm10Supply["min"] = 12.0f;
  pwm10Supply["max"] = 30.0f;
  pwm10Supply["unit"] = "V";
  pwm10Supply["current_mA"] = 100;
  JsonObject pwm10Input = pwm10.createNestedObject("inputLevel");
  pwm10Input["min"] = 4.5f;
  pwm10Input["max"] = 24.0f;
  pwm10Input["unit"] = "V";
  JsonObject pwm10Pwm = pwm10.createNestedObject("pwmRange");
  pwm10Pwm["min"] = 1000;
  pwm10Pwm["max"] = 3000;
  pwm10Pwm["unit"] = "Hz";
  JsonArray pwm10Modes = pwm10.createNestedArray("pwmModes");
  {
    JsonObject mode = pwm10Modes.createNestedObject();
    mode["id"] = "standard";
    mode["label"] = "Standard (≈2 kHz)";
    mode["frequency"] = 2000;
  }
  {
    JsonObject mode = pwm10Modes.createNestedObject();
    mode["id"] = "fast";
    mode["label"] = "Rapide (≈3 kHz)";
    mode["frequency"] = 3000;
  }
  JsonArray pwm10Pins = pwm10.createNestedArray("pins");
  for (const auto &pin : kPwmPins) {
    JsonObject pinObj = pwm10Pins.createNestedObject();
    pinObj["value"] = pin.value;
    pinObj["label"] = pin.label;
    pinObj["gpio"] = pin.gpio;
  }
  JsonObject pwm10Template = pwm10.createNestedObject("configTemplate");
  pwm10Template["pin"] = "D1";
  pwm10Template["pwmMode"] = "standard";
  pwm10Template["frequency"] = 2000;
  JsonObject pwm10TemplateRange =
      pwm10Template.createNestedObject("range");
  pwm10TemplateRange["min"] = 0.0f;
  pwm10TemplateRange["max"] = 10.0f;
  pwm10TemplateRange["unit"] = "V";
  JsonObject pwm10TemplateSupply =
      pwm10Template.createNestedObject("supply");
  pwm10TemplateSupply["voltage"] = 24.0f;
  pwm10TemplateSupply["unit"] = "V";
  JsonObject pwm10TemplateInput =
      pwm10Template.createNestedObject("inputLevel");
  pwm10TemplateInput["min"] = 4.5f;
  pwm10TemplateInput["max"] = 24.0f;
  pwm10TemplateInput["unit"] = "V";
  pwm10Template["jumper"] = "5V";
  pwm10Template["notes"] =
      "Alimenter le module entre 12 et 30 V et régler le potentiomètre.";
}

void IORegistry::snapshot(JsonDocument &doc) {
  doc.clear();
  JsonArray arr = doc.createNestedArray("channels");
  for (size_t i = 0; i < m_channelCount; i++) {
    const Channel &ch = m_channels[i];
    JsonObject obj = arr.createNestedObject();
    obj["id"] = ch.id;
    obj["type"] = ch.type;
    obj["index"] = ch.index;
    obj["k"] = ch.k;
    obj["b"] = ch.b;
    obj["unit"] = ch.unit;
    int32_t raw = readRaw(ch.id);
    obj["raw"] = raw;
    obj["value"] = convert(ch.id, raw);
  }
}

void IORegistry::describeChannels(JsonArray &arr) const {
  arr.clear();
  for (size_t i = 0; i < m_channelCount; i++) {
    const Channel &ch = m_channels[i];
    JsonObject obj = arr.createNestedObject();
    obj["id"] = ch.id;
    obj["type"] = ch.type;
    obj["index"] = ch.index;
    obj["k"] = ch.k;
    obj["b"] = ch.b;
    obj["unit"] = ch.unit;
  }
}