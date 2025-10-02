// UdpService listens for and broadcasts UDP packets containing IO values
// or commands. The implementation in this skeleton is minimal: it
// binds to a configurable port and logs incoming packets. In the
// future it can broadcast values to other MiniLabo devices or PCs
// and execute commands received over the network.

#ifndef MINILABOESP_UDPSERVICE_H
#define MINILABOESP_UDPSERVICE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>

class ConfigStore;
class IORegistry;
class Logger;

class UdpService {
public:
  UdpService(ConfigStore *config, IORegistry *ioReg, Logger *logger);
  void begin();
  void loop();
  bool isRunning() const { return m_running; }

  // Perform a discovery cycle to find other MiniLabo devices on the
  // network. Results are written to the provided JSON document as an
  // object containing a "devices" array. The function returns true if at
  // least one device responded. When the service is disabled the
  // document contains {"status":"udp_disabled","devices":[]}. The
  // timeout controls how long the scan waits for responses.
  bool discoverPeers(JsonDocument &doc, unsigned long timeoutMs = 600);

private:
  void handleIncomingPacket(const char *buf, int len, const IPAddress &ip,
                            uint16_t port);
  size_t applyRemoteValue(JsonVariantConst payload, const String &mac,
                          const String &hostname, const String &ip);
  void appendLocalInputs(JsonArray &arr);
  void sendDiscoveryReply(const IPAddress &ip, uint16_t port);

  WiFiUDP m_udp;
  uint16_t m_rxPort;
  uint16_t m_txPort;
  ConfigStore *m_config;
  IORegistry *m_io;
  Logger *m_logger;
  unsigned long m_lastSend;
  bool m_enabled;
  bool m_running;
};

#endif // MINILABOESP_UDPSERVICE_H
