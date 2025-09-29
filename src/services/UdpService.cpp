// Implementation of the UDP service

#include "UdpService.h"

#include "core/ConfigStore.h"
#include "core/IORegistry.h"
#include "core/Logger.h"
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
    const int bufSize = 256;
    char buf[bufSize];
    int len = m_udp.read(buf, bufSize - 1);
    if (len < 0) len = 0;
    if (len >= bufSize) len = bufSize - 1;
    buf[len] = '\0';
    if (m_logger) {
      m_logger->debug(String("UDP RX: ") + String(buf));
    }
    // TODO: parse commands and act on them
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