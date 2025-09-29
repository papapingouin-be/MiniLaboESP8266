// Implementation of the UDP service

#include "UdpService.h"

#include "core/ConfigStore.h"
#include "core/IORegistry.h"
#include "core/Logger.h"
#include <ArduinoJson.h>

UdpService::UdpService(ConfigStore *config, IORegistry *ioReg, Logger *logger)
    : m_rxPort(50000), m_txPort(50001), m_config(config), m_io(ioReg),
      m_logger(logger), m_lastSend(0) {}

void UdpService::begin() {
  // Bind to the receive port. If this fails there is little we can do.
  m_udp.begin(m_rxPort);
  if (m_logger)
    m_logger->info(String("UDP RX port ") + m_rxPort + " bound");
}

void UdpService::loop() {
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