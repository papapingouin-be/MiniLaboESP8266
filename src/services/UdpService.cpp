// Implementation of the UDP service

#include "UdpService.h"

#include "core/ConfigStore.h"
#include "core/IORegistry.h"
#include "core/Logger.h"
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>

UdpService::UdpService(ConfigStore *config, IORegistry *ioReg, Logger *logger)
    : m_rxPort(50000), m_txPort(50001), m_config(config), m_io(ioReg),
      m_logger(logger), m_lastSend(0), m_enabled(true), m_running(false) {}

void UdpService::begin() {
  if (m_config) {
    JsonDocument &doc = m_config->getConfig("udp");
    if (doc.is<JsonObject>()) {
      if (doc.containsKey("enabled")) {
        m_enabled = doc["enabled"].as<bool>();
      }
      if (doc.containsKey("port")) {
        m_rxPort = doc["port"].as<uint16_t>();
      }
      if (doc.containsKey("tx_port")) {
        m_txPort = doc["tx_port"].as<uint16_t>();
      }
    }
  }

  if (!m_enabled) {
    m_running = false;
    if (m_logger)
      m_logger->info("UDP service disabled by configuration");
    return;
  }

  // Bind to the receive port. If this fails there is little we can do.
  m_running = m_udp.begin(m_rxPort) == 1;
  if (m_logger) {
    if (m_running) {
      m_logger->info(String("UDP RX port ") + m_rxPort + " bound");
    } else {
      m_logger->error(String("Failed to bind UDP port ") + m_rxPort);
    }
  }
}

void UdpService::loop() {
  if (!m_running) {
    return;
  }
  int packetSize = m_udp.parsePacket();
  if (packetSize > 0) {
    // Read incoming packet
    const int bufSize = 384;
    char buf[bufSize];
    int len = m_udp.read(buf, bufSize - 1);
    if (len < 0) len = 0;
    if (len >= bufSize) len = bufSize - 1;
    buf[len] = '\0';
    if (m_logger) {
      m_logger->debug(String("UDP RX: ") + String(buf));
    }
    handleIncomingPacket(buf, len, m_udp.remoteIP(), m_udp.remotePort());
  }
  // Periodically broadcast a heartbeat with timestamp. In future this
  // could include IO values.
  unsigned long now = millis();
  if (now - m_lastSend >= 1000) {
    m_lastSend = now;
    StaticJsonDocument<128> doc;
    doc["ts"] = now;
    doc["msg"] = "heartbeat";
    String payload;
    serializeJson(doc, payload);
    m_udp.beginPacket(IPAddress(255, 255, 255, 255), m_txPort);
    m_udp.write((const uint8_t *)payload.c_str(), payload.length());
    m_udp.endPacket();
  }
}

void UdpService::handleIncomingPacket(const char *buf, int len,
                                      const IPAddress &ip, uint16_t port) {
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, buf, len);
  if (err) {
    if (m_logger) {
      m_logger->warning(String("UDP JSON parse error: ") + err.f_str());
    }
    return;
  }

  const char *cmd = doc["cmd"] | doc["type"] | "";
  if (!cmd || !strlen(cmd)) {
    return;
  }

  if (strcmp(cmd, "discover") == 0 || strcmp(cmd, "list_inputs") == 0) {
    sendDiscoveryReply(ip, port);
  }
}

void UdpService::appendLocalInputs(JsonArray &arr) {
  if (!m_io) {
    arr.clear();
    return;
  }
  m_io->describeChannels(arr);
}

void UdpService::sendDiscoveryReply(const IPAddress &ip, uint16_t port) {
  StaticJsonDocument<768> response;
  response["type"] = "discover_reply";
  response["mac"] = WiFi.macAddress();
  response["hostname"] = WiFi.hostname();
  response["ip"] = WiFi.localIP().toString();
  response["rx_port"] = m_rxPort;
  response["tx_port"] = m_txPort;
  JsonArray inputs = response.createNestedArray("inputs");
  appendLocalInputs(inputs);

  String payload;
  serializeJson(response, payload);
  m_udp.beginPacket(ip, port);
  m_udp.write(reinterpret_cast<const uint8_t *>(payload.c_str()),
              payload.length());
  m_udp.endPacket();
  if (m_logger) {
    m_logger->info(String("Sent UDP discovery reply to ") + ip.toString());
  }
}

bool UdpService::discoverPeers(JsonDocument &doc, unsigned long timeoutMs) {
  doc.clear();
  JsonArray devices = doc.createNestedArray("devices");
  if (!m_running) {
    doc["status"] = "udp_disabled";
    return false;
  }

  StaticJsonDocument<128> request;
  request["cmd"] = "discover";
  request["mac"] = WiFi.macAddress();
  String payload;
  serializeJson(request, payload);

  if (m_logger) {
    m_logger->info("Starting UDP discovery broadcast");
  }
  m_udp.beginPacket(IPAddress(255, 255, 255, 255), m_rxPort);
  m_udp.write(reinterpret_cast<const uint8_t *>(payload.c_str()),
              payload.length());
  m_udp.endPacket();

  unsigned long start = millis();
  unsigned long elapsed = 0;
  while ((elapsed = millis() - start) <= timeoutMs) {
    int packetSize = m_udp.parsePacket();
    if (packetSize > 0) {
      const int bufSize = 512;
      char buf[bufSize];
      int len = m_udp.read(buf, bufSize - 1);
      if (len < 0)
        len = 0;
      if (len >= bufSize)
        len = bufSize - 1;
      buf[len] = '\0';

      StaticJsonDocument<1024> reply;
      DeserializationError err = deserializeJson(reply, buf, len);
      if (err) {
        if (m_logger) {
          m_logger->warning(String("UDP discovery parse error: ") +
                            err.f_str());
        }
        continue;
      }

      const char *type = reply["type"] | reply["cmd"] | "";
      if (strcmp(type, "discover_reply") != 0) {
        // Let the regular loop handler process other message types.
        handleIncomingPacket(buf, len, m_udp.remoteIP(), m_udp.remotePort());
        continue;
      }

      String mac = reply["mac"] | String();
      String hostname = reply["hostname"] | String();
      String ip = reply["ip"] | m_udp.remoteIP().toString();
      uint16_t rxPort = reply["rx_port"] | m_rxPort;
      uint16_t txPort = reply["tx_port"] | m_txPort;

      JsonObject dest;
      if (mac.length()) {
        for (JsonVariant existingVar : devices) {
          JsonObject existing = existingVar.as<JsonObject>();
          const char *existingMac = existing["mac"] | "";
          if (existingMac && mac.equalsIgnoreCase(existingMac)) {
            dest = existing;
            break;
          }
        }
      }
      if (!dest.isNull()) {
        dest.clear();
      } else {
        dest = devices.createNestedObject();
      }
      dest["mac"] = mac;
      dest["hostname"] = hostname;
      dest["ip"] = ip;
      dest["rx_port"] = rxPort;
      dest["tx_port"] = txPort;
      JsonArray inputs = dest.createNestedArray("inputs");
      JsonArray payloadInputs = reply["inputs"].as<JsonArray>();
      if (!payloadInputs.isNull()) {
        for (JsonVariant entry : payloadInputs) {
          if (!entry.is<JsonObject>())
            continue;
          JsonObject src = entry.as<JsonObject>();
          JsonObject target = inputs.createNestedObject();
          target["id"] = src["id"] | "";
          target["type"] = src["type"] | "";
          target["index"] = src["index"] | 0;
          target["unit"] = src["unit"] | "";
          target["k"] = src["k"] | 0.0f;
          target["b"] = src["b"] | 0.0f;
        }
      }
      dest["lastSeenMs"] = elapsed;
    }
    delay(10);
  }

  doc["status"] = devices.size() ? "ok" : "no_devices";
  doc["elapsed_ms"] = elapsed;
  return devices.size() > 0;
}