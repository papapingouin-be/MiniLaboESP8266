// Implementation of the OLED display driver

#include "Oled.h"

#include "core/Logger.h"
#include "core/ConfigStore.h"
#include "services/UdpService.h"
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <Wire.h>

Oled::Oled(Logger *logger)
    : m_u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE), m_logger(logger) {}

void Oled::setConfigStore(ConfigStore *config) { m_config = config; }

void Oled::setUdpService(UdpService *udp) { m_udpService = udp; }

namespace {

const char *wifiStatusToString(wl_status_t status) {
  switch (status) {
  case WL_IDLE_STATUS:
    return "IDLE";
  case WL_NO_SSID_AVAIL:
    return "NO SSID";
  case WL_SCAN_COMPLETED:
    return "SCAN";
  case WL_CONNECTED:
    return "OK";
  case WL_CONNECT_FAILED:
    return "FAIL";
  case WL_CONNECTION_LOST:
    return "LOST";
  case WL_DISCONNECTED:
    return "DISC";
  default:
    return "UNK";
  }
}
} // namespace

void Oled::begin() {
  constexpr int kDefaultSdaPin = 12;
  constexpr int kDefaultSclPin = 14;
  constexpr uint8_t kDefaultAddress = 0x3C;
  constexpr uint32_t kDefaultClock = 100000;

  m_sdaPin = kDefaultSdaPin;
  m_sclPin = kDefaultSclPin;
  m_i2cAddress = kDefaultAddress;
  m_i2cClockHz = kDefaultClock;

  auto configureWire = [&](uint32_t clockHz) {
    if (m_sdaPin >= 0 && m_sclPin >= 0) {
      Wire.begin(m_sdaPin, m_sclPin);
    } else {
      Wire.begin();
    }
#if defined(ARDUINO_ARCH_ESP8266)
    // Many ESP8266 OLED modules rely on generous clock stretching during
    // initialisation. The default limit (around 230 µs) can cause the display
    // to miss commands, so increase it to a safe margin.
    Wire.setClockStretchLimit(150000);
#endif
    Wire.setClock(clockHz);
  };

  // Ensure the I2C bus is initialised. This allows us to probe the display
  // before attempting to configure U8g2 which provides clearer diagnostics on
  // the serial console when the screen is missing or miswired.
  configureWire(kDefaultClock);
  // Keep the default I2C clock (100 kHz) while probing. Some OLED modules
  // struggle to acknowledge requests at 400 kHz immediately after power-on,
  // which previously caused the detection logic to fail and left the screen
  // blank. The clock is increased after a device has been successfully
  // discovered.
  delay(50); // Allow devices time to power-up before probing

  const uint8_t candidateAddresses[] = {kDefaultAddress, 0x3D};
  uint8_t detected = 0;

  auto probeAddress = [](uint8_t address) -> bool {
    Wire.beginTransmission(address);
    return Wire.endTransmission() == 0;
  };

  auto detectWithRetry = [&](uint8_t address) {
    if (address < 0x03 || address > 0x77) {
      return false;
    }
    for (uint8_t attempt = 0; attempt < 3; ++attempt) {
      if (probeAddress(address)) {
        return true;
      }
      delay(20);
    }
    return false;
  };

  for (uint8_t addr : candidateAddresses) {
    if (detected != 0) {
      break;
    }
    if (detectWithRetry(addr)) {
      detected = addr;
    }
  }

  if (detected == 0) {
    Serial.println(F("[ERROR] OLED not detected on I2C bus"));
    if (m_logger) {
      m_logger->error("OLED display not found on the I2C bus");
    }
    return;
  }

  m_i2cAddress = detected;
  m_u8g2.setI2CAddress(m_i2cAddress << 1);

  configureWire(m_i2cClockHz);

  // Initialize the display. The hardware I2C pins are used. Some versions of
  // the U8g2 library reconfigure the Wire object during begin(), so apply the
  // desired pin configuration again afterwards to guarantee the custom SDA/SCL
  // mapping remains active.
  m_u8g2.begin();
  configureWire(m_i2cClockHz);
  m_available = true;
  m_u8g2.clearBuffer();
  m_u8g2.setFont(u8g2_font_6x10_tf);
  // Load the 4-digit PIN from configuration. Fall back to a random PIN
  // if configuration is unavailable.
  String configuredPin = resolveLoginPin();
  if (!configuredPin.isEmpty()) {
    m_pin = configuredPin;
  } else {
    randomSeed(micros());
    int pinNum = random(1000, 10000);
    m_pin = String(pinNum);
  }
  // Display a welcome message briefly
  m_u8g2.drawStr(0, 12, "MiniLaboESP");
  m_u8g2.drawStr(0, 24, "Starting...");
  m_u8g2.sendBuffer();

  if (m_logger) {
    String info = String("OLED ready @0x") + String(m_i2cAddress, HEX) +
                  ", session PIN: " + m_pin;
    if (m_sdaPin >= 0 && m_sclPin >= 0) {
      info += String(", SDA=") + m_sdaPin + ", SCL=" + m_sclPin;
    }
    info += String(", I2C ") + m_i2cClockHz + " Hz";
    m_logger->info(info);
  }
  Serial.print(F("[INFO] OLED initialised at 0x"));
  Serial.println(String(m_i2cAddress, HEX));
  if (m_sdaPin >= 0 && m_sclPin >= 0) {
    Serial.print(F("[INFO] OLED using SDA/SCL pins: "));
    Serial.print(m_sdaPin);
    Serial.print(F("/"));
    Serial.println(m_sclPin);
  }
  Serial.print(F("[INFO] OLED I2C clock: "));
  Serial.print(m_i2cClockHz);
  Serial.println(F(" Hz"));
}

void Oled::updateStatus() {
  if (!m_available) {
    return;
  }
  String configuredPin = resolveLoginPin();
  if (!configuredPin.isEmpty()) {
    m_pin = configuredPin;
  }
  size_t inputCount = 0;
  size_t outputCount = 0;
  computeIoCounts(inputCount, outputCount);
  bool udpRunning = m_udpService && m_udpService->isRunning();
  String wifiMode = currentWifiMode();

  m_u8g2.clearBuffer();
  m_u8g2.setFont(u8g2_font_6x10_tf);
  int y = 10;
  String modeLine = String("Mode: ") + wifiMode;
  m_u8g2.drawStr(0, y, modeLine.c_str());

  y += 10;
  String apLine = String("AP IP: ") + WiFi.softAPIP().toString();
  m_u8g2.drawStr(0, y, apLine.c_str());

  y += 10;
  wl_status_t staStatus = WiFi.status();
  String staLine;
  if (staStatus == WL_CONNECTED) {
    staLine = String("STA IP: ") + WiFi.localIP().toString();
  } else {
    staLine = String("STA ") + wifiStatusToString(staStatus);
  }
  m_u8g2.drawStr(0, y, staLine.c_str());

  y += 10;
  String pinLine = String("PIN: ") + (m_pin.isEmpty() ? String("----") : m_pin);
  m_u8g2.drawStr(0, y, pinLine.c_str());

  y += 10;
  String udpLine = String("UDP: ") + (udpRunning ? "ON" : "OFF");
  m_u8g2.drawStr(0, y, udpLine.c_str());

  y += 10;
  String ioLine = String("In:") + inputCount + String(" Out:") + outputCount;
  m_u8g2.drawStr(0, y, ioLine.c_str());
  m_u8g2.sendBuffer();
}

String Oled::resolveLoginPin() {
  if (!m_config) {
    return m_pin;
  }
  JsonDocument &doc = m_config->getConfig("network");
  String pinCandidate;
  if (doc.containsKey("login_pin")) {
    pinCandidate = doc["login_pin"].as<String>();
  }
  String digits;
  for (size_t i = 0; i < pinCandidate.length(); ++i) {
    char c = pinCandidate.charAt(i);
    if (c >= '0' && c <= '9') {
      digits += c;
    }
  }
  if (digits.length() > 4) {
    digits = digits.substring(0, 4);
  }
  if (digits.length() == 4) {
    return digits;
  }
  return String();
}

void Oled::computeIoCounts(size_t &inputs, size_t &outputs) {
  inputs = 0;
  outputs = 0;
  if (!m_config) {
    return;
  }

  auto countArray = [](JsonVariantConst var) -> size_t {
    if (!var.is<JsonArray>()) {
      return 0;
    }
    return var.size();
  };

  JsonDocument &ioDoc = m_config->getConfig("io");
  if (ioDoc.is<JsonArray>()) {
    JsonArray arr = ioDoc.as<JsonArray>();
    for (JsonVariant v : arr) {
      if (!v.is<JsonObject>()) {
        continue;
      }
      JsonObject obj = v.as<JsonObject>();
      String direction = obj["direction"].as<String>();
      if (direction.isEmpty()) {
        direction = obj["dir"].as<String>();
      }
      direction.toLowerCase();
      if (direction == "output" || direction == "out") {
        outputs++;
        continue;
      }
      if (direction == "input" || direction == "in") {
        inputs++;
        continue;
      }
      String type = obj["type"].as<String>();
      type.toLowerCase();
      if (type.indexOf("out") != -1 || type.indexOf("dac") != -1 ||
          type.indexOf("pwm") != -1) {
        outputs++;
      } else {
        inputs++;
      }
    }
  } else if (ioDoc.is<JsonObject>()) {
    JsonObject obj = ioDoc.as<JsonObject>();
    inputs = countArray(obj["inputs"]);
    outputs = countArray(obj["outputs"]);
  }

  if (inputs == 0) {
    JsonDocument &dmmDoc = m_config->getConfig("dmm");
    if (dmmDoc.is<JsonArray>()) {
      inputs = dmmDoc.size();
    }
  }

  if (outputs == 0) {
    JsonDocument &funcDoc = m_config->getConfig("funcgen");
    if (funcDoc.is<JsonArray>()) {
      outputs = funcDoc.size();
    } else if (funcDoc.is<JsonObject>() && funcDoc.size() > 0) {
      outputs = 1;
    }
  }
}

String Oled::currentWifiMode() const {
  WiFiMode_t mode = WiFi.getMode();
  bool ap = mode & WIFI_AP;
  bool sta = mode & WIFI_STA;
  if (ap && sta) {
    return "AP+STA";
  }
  if (ap) {
    return "AP";
  }
  if (sta) {
    return "STA";
  }
  return "OFF";
}

void Oled::showError(const String &msg) {
  if (!m_available) {
    Serial.println(String(F("[ERROR] Unable to display message on OLED: ")) +
                   msg);
    return;
  }
  m_u8g2.clearBuffer();
  m_u8g2.setFont(u8g2_font_6x10_tf);
  m_u8g2.drawStr(0, 12, "ERROR:");
  // Split message into lines of up to 20 characters
  int y = 24;
  int start = 0;
  while (start < (int)msg.length() && y < 64) {
    int len = 20;
    if (start + len > (int)msg.length()) len = msg.length() - start;
    String line = msg.substring(start, start + len);
    m_u8g2.drawStr(0, y, line.c_str());
    y += 12;
    start += len;
  }
  m_u8g2.sendBuffer();
}