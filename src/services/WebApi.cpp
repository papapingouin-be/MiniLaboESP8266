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
      "/api/dmm", HTTP_GET,
      [this]() {
        handleDmm();
      });
  m_server.on(
      "/api/funcgen", HTTP_POST,
      [this]() {
        handleFuncGen();
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

void WebApi::handlePutConfig() {
  if (!m_server.hasArg("area")) {
    m_server.send(400, "application/json",
                  "{\"error\":\"missing area parameter\"}");
    return;
  }
  String area = m_server.arg("area");
  // Get plain body
  String body = m_server.arg("plain");
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

void WebApi::handleFuncGen() {
  // Only accept JSON bodies
  String body = m_server.arg("plain");
  if (body.length() == 0) {
    m_server.send(400, "application/json",
                  "{\"error\":\"missing body\"}");
    return;
  }
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    m_server.send(400, "application/json",
                  String("{\"error\":\"invalid JSON: ") +
                      err.c_str() + "\"}");
    return;
  }
  m_funcGen->updateSettings(doc);
  m_server.send(200, "application/json", "{\"ok\":true}");
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