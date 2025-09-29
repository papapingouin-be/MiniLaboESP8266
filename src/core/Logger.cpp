// Implementation of the Logger class

#include "Logger.h"

Logger::Logger() {}

void Logger::begin() {
  // Open the log file in append mode. If it doesn't exist it will be
  // created. We don't truncate the file on each boot so logs from
  // previous sessions remain available. Consider rotation if the file
  // becomes too large.
  m_file = LittleFS.open("/logs.jsonl", "a");
  if (!m_file) {
    // If opening fails there is little we can do other than print to
    // Serial. This condition is not fatal because logging is best
    // effort.
    Serial.println(F("[WARN] Failed to open log file"));
  }
}

void Logger::log(Level level, const String &message) {
  // Construct a small JSON object for the log entry. Timestamp is
  // milliseconds since boot; level is string; msg is the provided
  // message. We do not include spans or transaction IDs in this
  // skeleton.
  StaticJsonDocument<256> doc;
  doc["ts"] = millis();
  doc["level"] = levelToString(level);
  doc["msg"] = message;
  // Serialize to buffer first so that we can write to both Serial and
  // file easily.
  String json;
  serializeJson(doc, json);
  // Write to Serial with human-readable prefix
  Serial.print("[");
  Serial.print(doc["level"].as<const char *>());
  Serial.print("] ");
  Serial.println(message);
  // Append to file if open
  if (m_file) {
    m_file.println(json);
    m_file.flush();
  }
}

const char *Logger::levelToString(Level lvl) {
  switch (lvl) {
  case Debug:
    return "D";
  case Info:
    return "I";
  case Warning:
    return "W";
  case Error:
    return "E";
  case Fatal:
    return "F";
  default:
    return "?";
  }
}

bool Logger::tail(size_t n, String &out) {
  out = "";
  // Open the log file for reading. We cannot reuse m_file because it
  // was opened in append mode.
  File f = LittleFS.open("/logs.jsonl", "r");
  if (!f) {
    return false;
  }
  // Read the entire file into memory. For small log files this is
  // acceptable. For larger files a more sophisticated implementation
  // would read from the end in chunks.
  String contents = f.readString();
  f.close();
  if (contents.length() == 0) {
    return true;
  }
  // Walk backwards through the string counting newline characters
  size_t len = contents.length();
  size_t count = 0;
  int startIndex = 0;
  for (int i = len - 1; i >= 0; i--) {
    if (contents[i] == '\n') {
      count++;
      if (count == n + 1) {
        startIndex = i + 1;
        break;
      }
    }
  }
  out = contents.substring(startIndex);
  return true;
}