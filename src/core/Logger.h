// Logger provides simple logging facilities for the MiniLaboESP
// firmware. Messages are written both to the serial port and a
// persistent file (logs.jsonl) on the filesystem. Each log entry is
// emitted in JSON-lines format so that external tools can parse
// structured logs. The logger also supports retrieving the last N
// entries for display via the web API.

#ifndef MINILABOESP_LOGGER_H
#define MINILABOESP_LOGGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

class Logger {
public:
  enum Level { Debug, Info, Warning, Error, Fatal };

  Logger();

  // Initialise the logger. Opens the log file for appending. Must be
  // called after LittleFS.begin().
  void begin();

  // Emit a message at the given level. The message should not
  // contain newlines. Internally this will write to Serial and
  // append a JSON object to the log file. Use the convenience
  // functions (debug/info/warning/error/fatal) instead of calling
  // this directly.
  void log(Level level, const String &message);

  // Convenience wrappers for common levels
  inline void debug(const String &msg) { log(Debug, msg); }
  inline void info(const String &msg) { log(Info, msg); }
  inline void warning(const String &msg) { log(Warning, msg); }
  inline void error(const String &msg) { log(Error, msg); }
  inline void fatal(const String &msg) { log(Fatal, msg); }

  // Retrieve the last n log entries as a String containing JSON lines.
  // Returns true on success. This will load the entire file into
  // memory so n should be reasonably small (<500). If the file does
  // not exist an empty string is returned.
  bool tail(size_t n, String &out);

private:
  File m_file;
  const char *levelToString(Level lvl);
};

#endif // MINILABOESP_LOGGER_H