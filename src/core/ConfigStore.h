// ConfigStore manages configuration files stored in JSON format on
// LittleFS. Each configuration area (general, network, io, dmm,
// funcgen, scope, math) is stored in its own file named
// "<area>.json" at the root of the filesystem. The class loads
// documents into memory on startup and provides access and update
// functions. Updates are written atomically by writing to a
// temporary file and renaming it over the original.

#ifndef MINILABOESP_CONFIGSTORE_H
#define MINILABOESP_CONFIGSTORE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

class ConfigStore {
public:
  ConfigStore();

  // Load all known configuration files. This should be called from
  // setup() after the filesystem has been mounted. Missing files
  // result in empty documents which callers can populate with
  // defaults.
  void begin();

  // Obtain a reference to a configuration document. If the area is not
  // known a new empty document is created and returned. The returned
  // document remains valid until the next call to begin().
  JsonDocument &getConfig(const String &area);

  // Update the configuration for the given area. The document is
  // serialized to JSON and atomically written to the corresponding
  // file. The in-memory copy is also updated. Returns true on
  // success.
  bool updateConfig(const String &area, const JsonDocument &doc);

private:
  struct Entry {
    String area;
    StaticJsonDocument<2048> doc;
    bool loaded;
  };
  // Fixed list of areas. Additional areas can be added here but
  // increasing this count also increases memory usage because each
  // entry reserves 2 KiB of static space.
  static const size_t kMaxAreas = 7;
  Entry m_entries[kMaxAreas];
  size_t m_count;
};

#endif // MINILABOESP_CONFIGSTORE_H