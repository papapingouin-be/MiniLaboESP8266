// UdpService listens for and broadcasts UDP packets containing IO values
// or commands. The implementation in this skeleton is minimal: it
// binds to a configurable port and logs incoming packets. In the
// future it can broadcast values to other MiniLabo devices or PCs
// and execute commands received over the network.

#ifndef MINILABOESP_UDPSERVICE_H
#define MINILABOESP_UDPSERVICE_H

#include <Arduino.h>
#include <WiFiUdp.h>

class ConfigStore;
class IORegistry;
class Logger;

class UdpService {
public:
  UdpService(ConfigStore *config, IORegistry *ioReg, Logger *logger);
  void begin();
  void loop();

private:
  WiFiUDP m_udp;
  uint16_t m_rxPort;
  uint16_t m_txPort;
  ConfigStore *m_config;
  IORegistry *m_io;
  Logger *m_logger;
  unsigned long m_lastSend;
};

#endif // MINILABOESP_UDPSERVICE_H