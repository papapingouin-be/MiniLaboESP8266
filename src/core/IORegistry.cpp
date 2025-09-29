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
  // Read configuration from io.json. The expected format is an array of
  // objects, each with at least an "id" and "type" field. Optional
  // fields include "index" (ADC channel number) and calibration
  // coefficients "k" and "b". If the file is missing or empty no
  // channels will be configured.
  JsonDocument &doc = m_config->getConfig("io");
  if (doc.is<JsonArray>()) {
    JsonArray arr = doc.as<JsonArray>();
    for (JsonVariant v : arr) {
      if (m_channelCount >= kMaxChannels) break;
      if (!v.is<JsonObject>()) continue;
      JsonObject obj = v.as<JsonObject>();
      Channel &ch = m_channels[m_channelCount++];
      ch.id = obj["id"] | String("ch") + String(m_channelCount);
      ch.type = obj["type"] | "a0";
      ch.index = obj["index"] | 0;
      ch.k = obj["k"] | 1.0;
      ch.b = obj["b"] | 0.0;
      const char *unit = obj["unit"] | "";
      ch.unit = unit;
    }
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
  JsonArray a0Indexes = a0.createNestedArray("indexes");
  JsonObject a0Index = a0Indexes.createNestedObject();
  a0Index["value"] = 0;
  a0Index["label"] = "A0";

  if (ensureAdsReady()) {
    JsonObject ads = locals.createNestedObject();
    ads["type"] = "ads1115";
    ads["label"] = "ADS1115";
    ads["defaultId"] = "ADS";
    ads["defaultUnit"] = "V";
    JsonArray adsIndexes = ads.createNestedArray("indexes");
    const char *labels[] = {"A0", "A1", "A2", "A3"};
    for (uint8_t i = 0; i < 4; i++) {
      JsonObject idx = adsIndexes.createNestedObject();
      idx["value"] = i;
      idx["label"] = labels[i];
    }
  }
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