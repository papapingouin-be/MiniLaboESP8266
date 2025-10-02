// Implementation of the DMM device driver

#include "Dmm.h"

#include "core/IORegistry.h"
#include "core/Logger.h"
#include "core/ConfigStore.h"

Dmm::Dmm(IORegistry *ioReg, Logger *logger, ConfigStore *config)
    : m_count(0), m_io(ioReg), m_logger(logger), m_config(config) {}

void Dmm::begin() {
  m_count = 0;
  // Read configuration from dmm.json. Expect an array of channel
  // definitions with fields: io (string), mode (string), decimals (int),
  // threshold (float), hyst (float). Missing fields use defaults.
  JsonDocument &doc = m_config->getConfig("dmm");
  if (doc.is<JsonArray>()) {
    JsonArray arr = doc.as<JsonArray>();
    for (JsonVariant v : arr) {
      if (m_count >= kMaxChannels) break;
      if (!v.is<JsonObject>()) continue;
      JsonObject obj = v.as<JsonObject>();
      Channel &ch = m_channels[m_count++];
      ch.ioId = obj["io"] | String();
      ch.mode = obj["mode"] | String("UDC");
      ch.decimals = obj["decimals"] | 2;
      ch.threshold = obj["threshold"] | 0.0;
      ch.hyst = obj["hyst"] | 0.0;
    }
  }
}

void Dmm::getSnapshot(JsonDocument &doc) {
  // Prepare an array in the provided document. The caller must
  // allocate sufficient capacity for the expected number of channels.
  JsonArray arr = doc.createNestedArray("channels");
  for (size_t i = 0; i < m_count; i++) {
    const Channel &ch = m_channels[i];
    float raw = m_io->readRaw(ch.ioId);
    float val = m_io->convert(ch.ioId, raw);
    // Round to the configured number of decimals
    float scale = 1.0;
    for (uint8_t j = 0; j < ch.decimals; j++) scale *= 10.0;
    val = roundf(val * scale) / scale;
    JsonObject obj = arr.createNestedObject();
    obj["id"] = ch.ioId;
    obj["raw"] = raw;
    obj["value"] = val;
    obj["unit"] = (ch.mode == "UDC") ? "V" : "";
  }
}