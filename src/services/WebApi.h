// WebApi exposes a simple HTTP server for configuration and data
// retrieval. It supports reading and writing configuration files,
// retrieving DMM snapshots, updating the function generator, fetching
// recent logs and serving static files from the filesystem.

#ifndef MINILABOESP_WEBAPI_H
#define MINILABOESP_WEBAPI_H

#include <Arduino.h>
#include <ESP8266WebServer.h>

class ConfigStore;
class IORegistry;
class Dmm;
class FuncGen;
class Logger;
class FileWriteService;

class WebApi {
public:
  WebApi(ConfigStore *config, IORegistry *ioReg, Dmm *dmm, FuncGen *funcGen,
         Logger *logger,
         FileWriteService *fileService);

  // Start the HTTP server and register handlers. Should be called
  // during setup().
  void begin();

  // Handle incoming client requests. Should be called frequently in
  // loop().
  void loop();

private:
  ConfigStore *m_config;
  IORegistry *m_io;
  Dmm *m_dmm;
  FuncGen *m_funcGen;
  Logger *m_logger;
  FileWriteService *m_fileService;
  ESP8266WebServer m_server;

  // Handler functions
  void handleGetConfig();
  void handlePutConfig();
  void handleDmm();
  void handleScope();
  void handleFuncGen();
  void handleLogsTail();
  void handleWifiScan();
  void handleIoHardware();
  void handleIoSnapshot();
  void handleOutputsTest();

  // Handle a login request. Accepts a JSON body containing a
  // "pin" field. The provided PIN is compared against the value
  // stored in the network configuration. If they match, the server
  // returns {"ok":true}. Otherwise it returns {"ok":false,
  // "error":"invalid pin"}. A real implementation should set a
  // session cookie and manage timeouts; this stub simply checks the
  // PIN.
  void handleLogin();

  // Expose pending write requests count
  void handleWriteQueue();
};

#endif // MINILABOESP_WEBAPI_H
