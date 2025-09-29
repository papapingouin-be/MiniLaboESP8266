// MiniLaboESP main firmware file
// This file sets up the microcontroller, initializes subsystems and runs
// a simple main loop. The goal of this skeleton is to provide a
// compilable baseline upon which additional features can be built.

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>

// Core and device headers
#include "core/ConfigStore.h"
#include "core/Logger.h"
#include "core/IORegistry.h"
#include "devices/Dmm.h"
#include "devices/Oled.h"
#include "devices/FuncGen.h"
#include "services/WebApi.h"
#include "services/UdpService.h"
#include "services/FileWriteService.h"

// Prefix for the access point SSID. A unique suffix will be
// appended based on the chip ID so that multiple boards can be
// identified on the air.
#define AP_SSID_PREFIX "MiniLabo"

// Serial console speed. Using 74880 baud keeps the boot ROM output and the
// firmware logs aligned, preventing the garbled characters that appear when the
// monitor is configured for a different speed.
#ifndef DEBUG_SERIAL_BAUD
#define DEBUG_SERIAL_BAUD 74880
#endif

// Instantiate global objects. These objects are created once and
// passed around to the various modules. They rely on each other via
// pointers rather than global state so that unit testing and
// dependency injection can be added later.
ConfigStore configStore;
Logger logger;
IORegistry ioRegistry(&logger);
Dmm dmm(&ioRegistry, &logger, &configStore);
Oled oled(&logger);
FuncGen funcGen(&logger, &configStore);
// Create the file write service. This service will queue file
// writes to avoid blocking the main loop. See FileWriteService for
// details.
FileWriteService fileWriteService;
WebApi webApi(&configStore, &ioRegistry, &dmm, &funcGen, &logger, &fileWriteService);
UdpService udpService(&configStore, &ioRegistry, &logger);

static bool g_wifiServicesEnabled = true;

// Timestamp used to throttle display updates. Updating the OLED too
// frequently can increase current draw and reduce lifetime.
static unsigned long lastDisplayUpdate = 0;

// Forward declarations for helper functions
static void setupWiFi();

void setup() {
  // Initialise serial for debugging. A small delay allows the UART to
  // stabilise before printing any messages.
  Serial.begin(DEBUG_SERIAL_BAUD);
  delay(100);

  // Mount the filesystem. LittleFS is chosen because it is reliable and
  // supports wear levelling. If mounting fails the device cannot
  // proceed safely so we log a fatal error and show it on the OLED.
  if (!LittleFS.begin()) {
    Serial.println(F("[ERROR] LittleFS mount failed"));
    logger.fatal("FS mount failed");
    oled.begin();
    oled.showError("FS mount failed");
    return;
  }

  // Start the logger. This opens the log file and records a boot event.
  logger.begin();
  logger.info("Booting MiniLaboESP");

  // Start file write service. This must be called after LittleFS
  // begins so that write operations succeed. Currently it does not
  // perform any setup but calling begin() makes intent explicit.
  fileWriteService.begin();

  // Load configuration. This will read all JSON configuration files
  // present on the filesystem. Missing files will result in empty
  // documents and defaults can be applied later.
  configStore.begin();

  // Set up networking in AP+STA mode based on the configuration.
  setupWiFi();

  // Initialise devices and services. Order is important: the OLED is
  // started early so that error messages can be displayed, then the
  // DMM, function generator and web/UDP services. Each module will
  // reference the configuration and logger as required.
  oled.setConfigStore(&configStore);
  oled.setUdpService(&udpService);
  oled.begin();
  dmm.begin();
  funcGen.begin();
  if (g_wifiServicesEnabled) {
    webApi.begin();
    udpService.begin();
  } else {
    logger.info("Network services disabled");
  }

  logger.info("Setup complete");
}

void loop() {
  // Process network requests. The web API handles HTTP endpoints and
  // serves static files from LittleFS. The UDP service receives and
  // transmits frames as required.
  if (g_wifiServicesEnabled) {
    webApi.loop();
    udpService.loop();
  }

  // Process queued file writes. Only one write is performed per
  // invocation to avoid blocking. This is essential to prevent
  // watchdog resets when configuration changes are saved.
  fileWriteService.loop();

  // Update devices. The DMM reads sensors, the function generator
  // updates its waveform and other periodic tasks can be added here.
  dmm.loop();
  funcGen.loop();

  // Update the OLED once per second. Rendering takes time and
  // refreshing faster does not improve usability for status messages.
  unsigned long now = millis();
  if (now - lastDisplayUpdate >= 1000) {
    lastDisplayUpdate = now;
    oled.updateStatus();
  }

  // Avoid starving other tasks. A small delay yields to WiFi and
  // allows asynchronous callbacks to run.
  delay(5);
}

static void setupWiFi() {
  // Determine WiFi mode from configuration. The network.json file can
  // specify "mode": "ap" or "sta". If not present we fall back to
  // default AP mode. When in station mode the device attempts to
  // connect to the specified SSID/passphrase but will still expose an
  // AP interface for configuration if the connection fails.
  JsonDocument &net = configStore.getConfig("network");
  const char *mode = net["mode"] | "ap";
  const char *staSsid = net["ssid"] | "";
  const char *staPassword = net["password"] | "";
  const char *apSsidCfg = net["ap_ssid"] | "";
  const char *apPassCfg = net["ap_password"] | "";
  const char *hostname = net["hostname"] | "";
  unsigned long staTimeout = net["sta_timeout_ms"] | 15000UL;

  if (strcmp(mode, "off") == 0 || strcmp(mode, "disabled") == 0) {
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();
    delay(1);
    g_wifiServicesEnabled = false;
    logger.info("WiFi disabled by configuration");
    Serial.println(F("[INFO] WiFi disabled via configuration"));
    return;
  }

  WiFi.persistent(false);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);

  if (strlen(hostname) > 0) {
    WiFi.hostname(hostname);
  }

  // Construct credentials for the access point. Use configuration if
  // provided, otherwise generate a unique SSID per device with a
  // sensible default password.
  String apSsid;
  if (strlen(apSsidCfg) > 0) {
    apSsid = apSsidCfg;
  } else if (strcmp(mode, "ap") == 0 && strlen(staSsid) > 0) {
    apSsid = staSsid;
  } else {
    apSsid = String(AP_SSID_PREFIX) + String(ESP.getChipId(), HEX);
  }

  String apPass;
  if (strlen(apPassCfg) > 0) {
    apPass = apPassCfg;
  }

  if (apPass.length() > 0 && apPass.length() < 8) {
    logger.warning("AP password shorter than 8 characters, starting open AP");
    apPass = "";
  } else if (apPass.isEmpty()) {
    logger.info("SoftAP configured without password (open network)");
  }

  Serial.println(String(F("[INFO] Starting SoftAP SSID: ")) + apSsid);
  bool apStarted = false;
  if (apPass.isEmpty()) {
    apStarted = WiFi.softAP(apSsid.c_str());
  } else {
    apStarted = WiFi.softAP(apSsid.c_str(), apPass.c_str());
  }
  if (apStarted) {
    logger.info(String("SoftAP started: ") + apSsid);
    Serial.print(F("[INFO] SoftAP IP address: "));
    Serial.println(WiFi.softAPIP());
  } else {
    logger.error(String("Failed to start SoftAP: ") + apSsid);
    Serial.println(F("[ERROR] SoftAP start failed"));
  }

  // If station credentials exist attempt to connect as a client. The
  // station mode allows the device to reach the internet or a local
  // router. We ignore connection failures in order to continue running
  // the AP for configuration.
  if (strcmp(mode, "sta") == 0 && strlen(staSsid) > 0) {
    Serial.println(String(F("[INFO] Connecting to WiFi network: ")) + staSsid);
    if (strlen(staPassword) > 0) {
      WiFi.begin(staSsid, staPassword);
    } else {
      WiFi.begin(staSsid);
    }
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < staTimeout) {
      Serial.print('.');
      delay(250);
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      logger.info(String("Connected to WiFi: ") + staSsid);
      Serial.print(F("[INFO] Station IP address: "));
      Serial.println(WiFi.localIP());
      WiFi.printDiag(Serial);
    } else {
      logger.warning(String("Failed to connect to WiFi: ") + staSsid);
      Serial.println(F("[WARN] Unable to connect as station"));
      Serial.print(F("[INFO] WiFi status code: "));
      Serial.println(WiFi.status());
    }
  }
}
