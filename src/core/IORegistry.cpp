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
      m_adsInitialized(false) {}

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
    m_ads = new Adafruit_ADS1115();
    if (beginAdsDevice(m_ads)) {
      // Use the default gain of +/-4.096V (1 bit = 0.125mV)
      m_ads->setGain(GAIN_ONE);
      m_adsInitialized = true;
      if (m_logger) m_logger->info("ADS1115 initialized");
    } else {
      m_adsInitialized = false;
      if (m_logger) m_logger->error("ADS1115 init failed");
    }
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
        if (m_adsInitialized) {
          int16_t val = m_ads->readADC_SingleEnded(ch.index);
          return (int32_t)val;
        } else {
          return 0;
        }
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