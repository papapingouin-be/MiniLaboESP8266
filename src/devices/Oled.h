// OLED display driver. Uses the U8g2 library to present status
// information to the user, including network SSID, access PIN and
// service states. A minimal error display function is provided to
// indicate fatal issues.

#ifndef MINILABOESP_OLED_H
#define MINILABOESP_OLED_H

#include <Arduino.h>
#include <U8g2lib.h>

class Logger;
class Oled {
public:
  explicit Oled(Logger *logger);

  // Initialise the OLED. Creates a random 4‑digit PIN for the
  // session and prepares the display. Must be called after wire
  // initialization (implicitly done by U8g2).
  void begin();

  // Update the status screen. Shows network SSID, PIN and a summary
  // of service states. Should be called periodically (e.g. once per
  // second).
  void updateStatus();

  // Display a critical error message. This clears the screen and
  // prints the provided message. Intended for use when a fatal
  // condition prevents the normal operation of the firmware.
  void showError(const String &msg);

private:
  U8G2_SSD1306_128X64_NONAME_F_HW_I2C m_u8g2;
  Logger *m_logger;
  String m_pin;
  uint8_t m_i2cAddress = 0x3C;
  bool m_available = false;
  int8_t m_sdaPin = -1;
  int8_t m_sclPin = -1;
  uint32_t m_i2cClockHz = 100000;
};

#endif // MINILABOESP_OLED_H