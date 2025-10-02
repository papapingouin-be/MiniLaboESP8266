// Implementation of the WebApi class

#include "WebApi.h"

#include "core/ConfigStore.h"
#include "core/IORegistry.h"
#include "core/Logger.h"
#include "devices/Dmm.h"
#include "devices/FuncGen.h"
#include "services/FileWriteService.h"
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>

namespace {
struct PinMapEntry {
  const char *label;
  uint8_t gpio;
};

const PinMapEntry kPinMap[] = {
    {"D0", 16}, {"D1", 5},  {"D2", 4},  {"D3", 0}, {"D4", 2},
    {"D5", 14}, {"D6", 12}, {"D7", 13}, {"D8", 15},
};

int pinLabelToGpio(const String &label) {
  for (const auto &entry : kPinMap) {
    if (label.equalsIgnoreCase(entry.label)) {
      return entry.gpio;
    }
  }

  String trimmed = label;
  trimmed.trim();
  if (!trimmed.length()) return -1;

  String upper = trimmed;
  upper.toUpperCase();
  if (upper.startsWith("GPIO")) {
    String numeric = upper.substring(4);
    bool digits = numeric.length() > 0;
    for (size_t i = 0; i < numeric.length() && digits; ++i) {
      char c = numeric.charAt(i);
      if (c < '0' || c > '9') digits = false;
    }
    if (digits) {
      int value = numeric.toInt();
      if (value >= 0 && value <= 16) return value;
    }
  }

  bool digits = true;
  for (size_t i = 0; i < trimmed.length() && digits; ++i) {
    char c = trimmed.charAt(i);
    if (c < '0' || c > '9') digits = false;
  }
  if (digits) {
    int value = trimmed.toInt();
    if (value >= 0 && value <= 16) return value;
  }

  return -1;
}
} // namespace

WebApi::WebApi(ConfigStore *config, IORegistry *ioReg, Dmm *dmm,
               FuncGen *funcGen, Logger *logger,
               FileWriteService *fileService)
    : m_config(config), m_io(ioReg), m_dmm(dmm), m_funcGen(funcGen),
      m_logger(logger), m_fileService(fileService), m_server(80) {}

void WebApi::begin() {
  // Register handlers for API endpoints
  m_server.on(
      "/api/config", HTTP_GET,
      [this]() {
        handleGetConfig();
      });
  m_server.on(
      "/api/config", HTTP_PUT,
      [this]() {
        handlePutConfig();
      });
  m_server.on(
      "/api/io/hardware", HTTP_GET,
      [this]() {
        handleIoHardware();
      });
  m_server.on(
      "/api/io/snapshot", HTTP_GET,
      [this]() {
        handleIoSnapshot();
      });
  m_server.on(
      "/api/outputs/test", HTTP_POST,
      [this]() {
        handleOutputsTest();
      });
  m_server.on(
      "/api/dmm", HTTP_GET,
      [this]() {
        handleDmm();
      });
  m_server.on(
      "/api/scope", HTTP_GET,
      [this]() {
        handleScope();
      });
  m_server.on(
      "/api/funcgen", HTTP_GET,
      [this]() {
        handleFuncGenGet();
      });
  m_server.on(
      "/api/funcgen", HTTP_POST,
      [this]() {
        handleFuncGenPost();
      });
  m_server.on(
      "/api/logs/tail", HTTP_GET,
      [this]() {
        handleLogsTail();
      });

  // Endpoint to get the number of pending file writes. Returns JSON
  // {"pending": <number>}
  m_server.on(
      "/api/writequeue", HTTP_GET,
      [this]() {
        handleWriteQueue();
      });

  m_server.on(
      "/api/wifi/scan", HTTP_GET,
      [this]() {
        handleWifiScan();
      });

  // Endpoint for login. Expects a JSON body { "pin": "1234" }
  // and compares it to the PIN stored in network.json. See
  // handleLogin() for details.
  m_server.on(
      "/api/login", HTTP_POST,
      [this]() {
        handleLogin();
      });
  // Serve the main web application from LittleFS. Register an explicit
  // handler for the root path so we can return index.html while still
  // exposing the rest of the files in the LittleFS filesystem via
  // serveStatic. The explicit handler is necessary because
  // ESP8266WebServer::serveStatic returns void and therefore does not
  // support chaining setDefaultFile like AsyncWebServer does.
  m_server.on(
      "/", HTTP_GET,
      [this]() {
        if (!LittleFS.exists("/index.html")) {
          m_server.send(500, "text/plain",
                        "index.html not found in LittleFS");
          return;
        }
        fs::File file = LittleFS.open("/index.html", "r");
        if (!file) {
          m_server.send(500, "text/plain",
                        "Failed to open index.html");
          return;
        }
        m_server.streamFile(file, "text/html");
        file.close();
      });
  m_server.serveStatic("/", LittleFS, "/");
  // Start the server
  m_server.begin();
  if (m_logger) m_logger->info("HTTP server started");
}

void WebApi::loop() {
  m_server.handleClient();
}

void WebApi::handleGetConfig() {
  // Expect query parameter 'area' specifying which config to retrieve
  if (!m_server.hasArg("area")) {
    m_server.send(400, "application/json",
                  "{\"error\":\"missing area parameter\"}");
    return;
  }
  String area = m_server.arg("area");
  JsonDocument &doc = m_config->getConfig(area);
  String response;
  serializeJson(doc, response);
  m_server.send(200, "application/json", response);
}

void WebApi::handleIoHardware() {
  if (!m_io) {
    m_server.send(500, "application/json",
                  "{\"error\":\"io unavailable\"}");
    return;
  }
  StaticJsonDocument<512> doc;
  m_io->describeHardware(doc);
  String response;
  serializeJson(doc, response);
  m_server.send(200, "application/json", response);
}

void WebApi::handleIoSnapshot() {
  if (!m_io) {
    m_server.send(500, "application/json",
                  "{\"error\":\"io unavailable\"}");
    return;
  }
  DynamicJsonDocument doc(4096);
  m_io->snapshot(doc);
  if (doc.overflowed()) {
    if (m_logger) {
      m_logger->error("IO snapshot JSON overflow");
    }
    m_server.send(500, "application/json",
                  "{\"error\":\"snapshot too large\"}");
    return;
  }
  String response;
  serializeJson(doc, response);
  m_server.send(200, "application/json", response);
}

void WebApi::handleOutputsTest() {
  String body = m_server.arg("plain");
  if (!body.length()) {
    m_server.send(400, "application/json",
                  "{\"error\":\"missing body\"}");
    return;
  }

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    m_server.send(400, "application/json",
                  String("{\"error\":\"invalid JSON: ") + err.c_str() + "\"}");
    return;
  }

  const char *pinRaw = doc["pin"];
  if (!pinRaw || pinRaw[0] == '\0') {
    m_server.send(400, "application/json",
                  "{\"error\":\"missing pin\"}");
    return;
  }

  String pinLabel = String(pinRaw);
  pinLabel.trim();
  int gpio = pinLabelToGpio(pinLabel);
  if (gpio < 0) {
    m_server.send(400, "application/json",
                  "{\"error\":\"unsupported pin\"}");
    return;
  }

  if (m_logger) {
    m_logger->info(String(F("Test 5 Hz sur ")) + pinLabel + F(" (GPIO") +
                   String(gpio) + F(")"));
  }

  pinMode(gpio, OUTPUT);
  digitalWrite(gpio, LOW);

  const uint8_t cycles = 10; // 2 secondes à 5 Hz
  for (uint8_t i = 0; i < cycles; ++i) {
    digitalWrite(gpio, HIGH);
    delay(100);
    digitalWrite(gpio, LOW);
    delay(100);
  }

  digitalWrite(gpio, LOW);

  StaticJsonDocument<64> responseDoc;
  responseDoc["ok"] = true;
  String response;
  serializeJson(responseDoc, response);
  m_server.send(200, "application/json", response);
}

void WebApi::handlePutConfig() {
  if (!m_server.hasArg("area")) {
    m_server.send(400, "application/json",
                  "{\"error\":\"missing area parameter\"}");
    return;
  }
  String area = m_server.arg("area");
  // Get plain body
  String body = m_server.arg("plain");
  Serial.println(String(F("[HTTP] PUT /api/config area=")) + area +
                 F(" length=") + String(body.length()));
  if (body.length() == 0) {
    m_server.send(400, "application/json",
                  "{\"error\":\"missing body\"}");
    return;
  }
  // Parse JSON
  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    m_server.send(400, "application/json",
                  String("{\"error\":\"invalid JSON: ") +
                      err.c_str() + "\"}");
    return;
  }
  // Update configuration in memory only
  {
    JsonDocument &dest = m_config->getConfig(area);
    dest.clear();
    dest.set(doc);
  }
  // Enqueue file write via FileWriteService. If unavailable, fall
  // back to direct update. We construct the JSON string once.
  String out;
  serializeJson(doc, out);
  String filename = "/" + area + String(".json");
  if (m_fileService) {
    m_fileService->enqueue(filename, out);
  } else {
    // Fallback: write directly (may block and cause reboot). TODO: remove when FileWriteService is integrated everywhere.
    File f = LittleFS.open(filename + ".tmp", "w");
    if (f) {
      f.print(out);
      f.flush();
      f.close();
      LittleFS.remove(filename);
      LittleFS.rename(filename + ".tmp", filename);
      Serial.println(String(F("[HTTP] Direct write complete: ")) + filename);
    } else {
      Serial.println(String(F("[HTTP] Direct write failed to open: ")) +
                     filename);
    }
  }
  m_server.send(200, "application/json", "{\"ok\":true}");
}

void WebApi::handleDmm() {
  // Build snapshot document
  StaticJsonDocument<512> doc;
  m_dmm->getSnapshot(doc);
  String response;
  serializeJson(doc, response);
  m_server.send(200, "application/json", response);
}

void WebApi::handleScope() {
  if (!m_io) {
    m_server.send(500, "application/json",
                  "{\"error\":\"io unavailable\"}");
    return;
  }

  struct ChannelDef {
    String name;
    String io;
    String label;
    String display;
  };

  ChannelDef channels[4];
  size_t channelCount = 0;
  size_t sampleCount = 200;
  float timebaseMsPerDiv = 10.0f;
  float voltsPerDiv = 1.0f;
  String defaultChannel = "CH1";
  String defaultIo = "A0";
  String defaultLabel;
  String defaultDisplay;

  auto addChannel = [&](const String &name, const String &io,
                        const String &label, const String &display) {
    if (channelCount >= (sizeof(channels) / sizeof(channels[0]))) return;
    ChannelDef &ch = channels[channelCount];
    ch.name = name.length() ? name : String("CH") + String(channelCount + 1);
    ch.io = io.length() ? io : defaultIo;
    ch.label = label.length() ? label : ch.name;
    if (display.length()) {
      ch.display = display;
    } else {
      ch.display = ch.label;
      if (ch.io.length()) {
        ch.display += String(" — ") + ch.io;
      }
    }
    channelCount++;
  };

  auto channelExists = [&](const String &name) {
    for (size_t i = 0; i < channelCount; ++i) {
      if (channels[i].name == name) return true;
    }
    return false;
  };

  if (m_config) {
    JsonDocument &cfgDoc = m_config->getConfig("scope");
    if (cfgDoc.is<JsonObject>()) {
      JsonObject cfg = cfgDoc.as<JsonObject>();

      if (cfg["timebase_ms_per_div"].is<float>()) {
        timebaseMsPerDiv = cfg["timebase_ms_per_div"].as<float>();
      } else if (cfg["timebase"].is<float>()) {
        timebaseMsPerDiv = cfg["timebase"].as<float>();
      } else if (cfg["ms_per_div"].is<float>()) {
        timebaseMsPerDiv = cfg["ms_per_div"].as<float>();
      }
      if (cfg["volts_per_div"].is<float>()) {
        voltsPerDiv = cfg["volts_per_div"].as<float>();
      } else if (cfg["vdiv"].is<float>()) {
        voltsPerDiv = cfg["vdiv"].as<float>();
      }

      if (cfg["sample_count"].is<uint32_t>()) {
        sampleCount = cfg["sample_count"].as<uint32_t>();
      } else if (cfg["samples_per_frame"].is<uint32_t>()) {
        sampleCount = cfg["samples_per_frame"].as<uint32_t>();
      } else if (cfg["samples"].is<uint32_t>()) {
        sampleCount = cfg["samples"].as<uint32_t>();
      } else if (cfg["points"].is<uint32_t>()) {
        sampleCount = cfg["points"].as<uint32_t>();
      }

      if (cfg["channel"].is<const char *>()) {
        const char *ch = cfg["channel"].as<const char *>();
        if (ch && ch[0]) defaultChannel = String(ch);
      }
      if (cfg["io"].is<const char *>()) {
        const char *io = cfg["io"].as<const char *>();
        if (io && io[0]) defaultIo = String(io);
      } else if (cfg["input"].is<const char *>()) {
        const char *io = cfg["input"].as<const char *>();
        if (io && io[0]) defaultIo = String(io);
      }
      if (cfg["label"].is<const char *>()) {
        const char *label = cfg["label"].as<const char *>();
        if (label && label[0]) defaultLabel = String(label);
      }
      if (cfg["display"].is<const char *>()) {
        const char *display = cfg["display"].as<const char *>();
        if (display && display[0]) defaultDisplay = String(display);
      }

      if (cfg.containsKey("channels")) {
        JsonArray arr = cfg["channels"].as<JsonArray>();
        for (JsonVariant v : arr) {
          if (!v.is<JsonObject>()) continue;
          JsonObject chObj = v.as<JsonObject>();
          String name;
          if (chObj["channel"].is<const char *>()) {
            const char *c = chObj["channel"].as<const char *>();
            if (c && c[0]) name = String(c);
          } else if (chObj["name"].is<const char *>()) {
            const char *c = chObj["name"].as<const char *>();
            if (c && c[0]) name = String(c);
          } else if (chObj["id"].is<const char *>()) {
            const char *c = chObj["id"].as<const char *>();
            if (c && c[0]) name = String(c);
          }
          String io;
          if (chObj["io"].is<const char *>()) {
            const char *c = chObj["io"].as<const char *>();
            if (c && c[0]) io = String(c);
          } else if (chObj["input"].is<const char *>()) {
            const char *c = chObj["input"].as<const char *>();
            if (c && c[0]) io = String(c);
          }
          String label;
          if (chObj["label"].is<const char *>()) {
            const char *c = chObj["label"].as<const char *>();
            if (c && c[0]) label = String(c);
          }
          String display;
          if (chObj["display"].is<const char *>()) {
            const char *c = chObj["display"].as<const char *>();
            if (c && c[0]) display = String(c);
          }
          if (!channelExists(name)) {
            addChannel(name, io, label, display);
          }
          if (name == defaultChannel) {
            if (io.length()) defaultIo = io;
            if (!defaultLabel.length() && label.length()) defaultLabel = label;
            if (!defaultDisplay.length() && display.length())
              defaultDisplay = display;
          }
          if (channelCount >= (sizeof(channels) / sizeof(channels[0])))
            break;
        }
      }

      if (cfg.containsKey("channel_map")) {
        JsonObject map = cfg["channel_map"].as<JsonObject>();
        for (JsonPair kv : map) {
          String name = String(kv.key().c_str());
          if (channelExists(name)) continue;
          String io;
          String label;
          String display;
          JsonVariant value = kv.value();
          if (value.is<const char *>()) {
            const char *c = value.as<const char *>();
            if (c && c[0]) io = String(c);
          } else if (value.is<JsonObject>()) {
            JsonObject obj = value.as<JsonObject>();
            if (obj["io"].is<const char *>()) {
              const char *c = obj["io"].as<const char *>();
              if (c && c[0]) io = String(c);
            }
            if (obj["label"].is<const char *>()) {
              const char *c = obj["label"].as<const char *>();
              if (c && c[0]) label = String(c);
            }
            if (obj["display"].is<const char *>()) {
              const char *c = obj["display"].as<const char *>();
              if (c && c[0]) display = String(c);
            }
          }
          addChannel(name, io, label, display);
          if (name == defaultChannel) {
            if (io.length()) defaultIo = io;
            if (!defaultLabel.length() && label.length()) defaultLabel = label;
            if (!defaultDisplay.length() && display.length())
              defaultDisplay = display;
          }
          if (channelCount >= (sizeof(channels) / sizeof(channels[0])))
            break;
        }
      }
    }
  }

  if (!channelExists(defaultChannel)) {
    addChannel(defaultChannel, defaultIo, defaultLabel, defaultDisplay);
  }

  if (sampleCount < 32) sampleCount = 32;
  if (sampleCount > 400) sampleCount = 400;
  if (timebaseMsPerDiv <= 0.0f) timebaseMsPerDiv = 10.0f;
  if (voltsPerDiv <= 0.0f) voltsPerDiv = 1.0f;
  if (defaultIo.length()) {
    // Ensure fallback IO is propagated to all channels lacking a mapping.
    for (size_t i = 0; i < channelCount; ++i) {
      if (!channels[i].io.length()) channels[i].io = defaultIo;
    }
  }

  if (channelCount == 0) {
    m_server.send(500, "application/json",
                  "{\"error\":\"no scope channels\"}");
    return;
  }

  size_t docCapacity = 1024 + channelCount * sampleCount * 16;
  if (docCapacity < 4096) docCapacity = 4096;
  DynamicJsonDocument doc(docCapacity);
  JsonObject root = doc.to<JsonObject>();
  root["timebase_ms_per_div"] = timebaseMsPerDiv;
  root["volts_per_div"] = voltsPerDiv;
  JsonObject channelsObj = root.createNestedObject("channels");

  JsonArray sampleArrays[sizeof(channels) / sizeof(channels[0])];
  for (size_t i = 0; i < channelCount; ++i) {
    JsonObject chObj = channelsObj.createNestedObject(channels[i].name);
    chObj["label"] = channels[i].label;
    chObj["display"] = channels[i].display;
    chObj["io"] = channels[i].io;
    sampleArrays[i] = chObj.createNestedArray("samples");
  }

  float totalSpanUs = timebaseMsPerDiv * 1000.0f * 10.0f;
  uint32_t intervalUs = 0;
  if (sampleCount > 1 && totalSpanUs > 0.0f) {
    float raw = totalSpanUs / static_cast<float>(sampleCount - 1);
    if (raw > 0.0f) {
      intervalUs = static_cast<uint32_t>(raw);
    }
  }

  auto waitInterval = [&](uint32_t us) {
    if (us == 0) return;
    if (us >= 1000) {
      delay(us / 1000);
      us %= 1000;
    }
    if (us > 0) delayMicroseconds(us);
  };

  for (size_t i = 0; i < sampleCount; ++i) {
    for (size_t c = 0; c < channelCount; ++c) {
      const ChannelDef &ch = channels[c];
      int32_t raw = m_io->readRaw(ch.io);
      float value = m_io->convert(ch.io, raw);
      sampleArrays[c].add(value);
    }
    if (i + 1 < sampleCount) {
      if (intervalUs > 0) {
        waitInterval(intervalUs);
      } else {
        // Yield to keep Wi-Fi responsive during fast captures.
        delay(0);
      }
    }
    if ((i & 0x1F) == 0) {
      yield();
    }
  }

  String response;
  serializeJson(doc, response);
  m_server.send(200, "application/json", response);
}

void WebApi::handleFuncGenGet() {
  StaticJsonDocument<512> resp;
  JsonObject root = resp.to<JsonObject>();
  if (m_funcGen) {
    m_funcGen->snapshotStatus(root);
  }
  root["ok"] = true;
  String body;
  serializeJson(resp, body);
  if (m_logger) {
    m_logger->debug(String(F("HTTP GET /api/funcgen => ")) + body);
  }
  m_server.send(200, "application/json", body);
}

void WebApi::handleFuncGenPost() {
  // Only accept JSON bodies
  String body = m_server.arg("plain");
  if (body.length() == 0) {
    m_server.send(400, "application/json",
                  "{\"error\":\"missing body\"}");
    return;
  }
  if (m_logger) {
    m_logger->info(String(F("HTTP POST /api/funcgen body=")) + body);
  }
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    m_server.send(400, "application/json",
                  String("{\"error\":\"invalid JSON: ") +
                      err.c_str() + "\"}");
    if (m_logger) {
      m_logger->error(String(F("FuncGen POST JSON error: ")) + err.c_str());
    }
    return;
  }
  m_funcGen->updateSettings(doc);
  StaticJsonDocument<512> resp;
  resp["ok"] = true;
  resp["success"] = true;
  JsonObject status = resp.createNestedObject("status");
  if (m_funcGen) {
    m_funcGen->snapshotStatus(status);
  }
  if (status.containsKey("enabled")) {
    resp["enabled"] = status["enabled"];
  }
  if (status.containsKey("target")) {
    resp["target"] = status["target"];
  }
  if (status.containsKey("summary")) {
    resp["summary"] = status["summary"];
    resp["message"] = status["summary"];
  }
  String responseBody;
  serializeJson(resp, responseBody);
  if (m_logger) {
    m_logger->info(String(F("FuncGen POST ack=")) + responseBody);
  }
  m_server.send(200, "application/json", responseBody);
}

void WebApi::handleLogsTail() {
  // Parameter n determines how many lines to return. Default 100.
  int n = 100;
  if (m_server.hasArg("n")) {
    n = m_server.arg("n").toInt();
    if (n <= 0) n = 1;
    if (n > 500) n = 500;
  }
  String out;
  if (!m_logger->tail(n, out)) {
    m_server.send(500, "application/json",
                  "{\"error\":\"failed to read logs\"}");
    return;
  }
  m_server.send(200, "text/plain", out);
}

void WebApi::handleWriteQueue() {
  if (!m_fileService) {
    m_server.send(500, "application/json",
                  "{\"error\":\"file service not available\"}");
    return;
  }
  StaticJsonDocument<64> doc;
  doc["pending"] = m_fileService->pending();
  String resp;
  serializeJson(doc, resp);
  m_server.send(200, "application/json", resp);
}

void WebApi::handleWifiScan() {
  int16_t count = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
  if (count < 0) {
    m_server.send(500, "application/json",
                  "{\"error\":\"scan failed\"}");
    return;
  }

  DynamicJsonDocument doc(4096);
  JsonArray arr = doc.to<JsonArray>();
  for (int16_t i = 0; i < count; ++i) {
    JsonObject obj = arr.createNestedObject();
    obj["ssid"] = WiFi.SSID(i);
    obj["rssi"] = WiFi.RSSI(i);
    obj["channel"] = WiFi.channel(i);
    obj["hidden"] = WiFi.isHidden(i);
    auto enc = WiFi.encryptionType(i);
    obj["secure"] = enc != ENC_TYPE_NONE;
    switch (enc) {
    case ENC_TYPE_WEP:
      obj["encryption"] = "WEP";
      break;
    case ENC_TYPE_TKIP:
      obj["encryption"] = "WPA/TKIP";
      break;
    case ENC_TYPE_CCMP:
      obj["encryption"] = "WPA2/CCMP";
      break;
    case ENC_TYPE_AUTO:
      obj["encryption"] = "AUTO";
      break;
    case ENC_TYPE_NONE:
      obj["encryption"] = "open";
      break;
    default:
      obj["encryption"] = "unknown";
      break;
    }
  }
  WiFi.scanDelete();
  String out;
  serializeJson(arr, out);
  m_server.send(200, "application/json", out);
}

void WebApi::handleLogin() {
  // Expect a JSON body with a "pin" field
  String body = m_server.arg("plain");
  if (body.length() == 0) {
    m_server.send(400, "application/json",
                  "{\"error\":\"missing body\"}");
    return;
  }
  StaticJsonDocument<64> doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    m_server.send(400, "application/json",
                  String("{\"error\":\"invalid JSON: ") + err.c_str() + "\"}");
    return;
  }
  // Extract provided PIN as string to allow leading zeros
  const char *pinValue = doc["pin"].as<const char *>();
  if (!pinValue) {
    m_server.send(400, "application/json",
                  "{\"error\":\"missing pin\"}");
    return;
  }
  String providedPin(pinValue);
  providedPin.trim();
  String cleanedProvided;
  for (size_t i = 0; i < providedPin.length(); ++i) {
    char c = providedPin[i];
    if (c >= '0' && c <= '9') cleanedProvided += c;
  }
  if (cleanedProvided.length() != 4) {
    m_server.send(400, "application/json",
                  "{\"error\":\"pin must be 4 digits\"}");
    return;
  }
  // Load stored PIN from network config. If missing, treat as no PIN.
  JsonDocument &ndoc = m_config->getConfig("network");
  String storedPin = ndoc["login_pin"].as<String>();
  String cleanedStored;
  for (size_t i = 0; i < storedPin.length(); ++i) {
    char c = storedPin[i];
    if (c >= '0' && c <= '9') cleanedStored += c;
  }
  if (cleanedStored.length() != 4) {
    // If no valid pin is configured, accept the provided one and persist it.
    storedPin = cleanedProvided;
    ndoc["login_pin"] = storedPin;
    String out;
    serializeJson(ndoc, out);
    String filename = "/network.json";
    if (m_fileService) {
      m_fileService->enqueue(filename, out);
    } else {
      File f = LittleFS.open(filename + ".tmp", "w");
      if (f) {
        f.print(out);
        f.flush();
        f.close();
        LittleFS.remove(filename);
        LittleFS.rename(filename + ".tmp", filename);
      }
    }
    StaticJsonDocument<32> resp;
    resp["ok"] = true;
    String s;
    serializeJson(resp, s);
    m_server.send(200, "application/json", s);
    return;
  }
  storedPin = cleanedStored;
  bool match = (cleanedProvided == storedPin);
  StaticJsonDocument<64> resp;
  resp["ok"] = match;
  if (!match) {
    resp["error"] = "invalid pin";
  }
  String out;
  serializeJson(resp, out);
  m_server.send(200, "application/json", out);
}