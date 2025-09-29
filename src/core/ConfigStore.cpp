// Implementation of the ConfigStore class

#include "ConfigStore.h"

ConfigStore::ConfigStore() : m_count(0) {}

void ConfigStore::begin() {
  // Define a static list of configuration areas. If more areas are
  // needed they can be added here. Each will reserve a 2 KiB JSON
  // document for its contents.
  static const char *defaultAreas[] = {
      "general",
      "network",
      "io",
      "dmm",
      "funcgen",
      "scope",
      "math",
  };

  m_count = 0;
  // Iterate through the list and try to load each file.
  for (size_t i = 0; i < kMaxAreas; i++) {
    if (i >= sizeof(defaultAreas) / sizeof(defaultAreas[0]))
      break;
    Entry &e = m_entries[m_count++];
    e.area = String(defaultAreas[i]);
    e.doc.clear();
    e.loaded = false;
    String filename = "/" + e.area + String(".json");
    if (LittleFS.exists(filename)) {
      File f = LittleFS.open(filename, "r");
      if (f) {
        DeserializationError err = deserializeJson(e.doc, f);
        if (!err) {
          e.loaded = true;
        }
        f.close();
      }
    }
  }
}

JsonDocument &ConfigStore::getConfig(const String &area) {
  // Search for an existing entry. Case-sensitive match on the area
  // string. If found, return the document reference. Otherwise create
  // a new empty entry if there is space.
  for (size_t i = 0; i < m_count; i++) {
    if (m_entries[i].area == area) {
      return m_entries[i].doc;
    }
  }
  // Add new entry if there is room
  if (m_count < kMaxAreas) {
    Entry &e = m_entries[m_count++];
    e.area = area;
    e.doc.clear();
    e.loaded = false;
    return e.doc;
  }
  // As a last resort, return the first entry. This should not
  // normally happen because the number of areas is fixed and
  // controlled by begin().
  return m_entries[0].doc;
}

bool ConfigStore::updateConfig(const String &area, const JsonDocument &doc) {
  // Find the corresponding entry so we can update the in-memory copy.
  size_t index = kMaxAreas; // invalid
  for (size_t i = 0; i < m_count; i++) {
    if (m_entries[i].area == area) {
      index = i;
      break;
    }
  }
  if (index == kMaxAreas) {
    // Unknown area
    return false;
  }
  // Construct file names. We write to a temporary file first to
  // guarantee atomic replacement. Once the write succeeds we rename
  // the file to the target name.
  String filename = "/" + area + String(".json");
  String tmpname = filename + ".tmp";
  // Open temporary file for writing
  File f = LittleFS.open(tmpname, "w");
  if (!f) {
    return false;
  }
  // Serialize JSON into the file
  if (serializeJson(doc, f) == 0) {
    f.close();
    LittleFS.remove(tmpname);
    return false;
  }
  f.flush();
  f.close();
  // Remove the original file (ignore failures) and rename the temp
  // file. On POSIX systems rename is atomic; on LittleFS it is
  // implemented as copy+delete so an interruption can leave a temp
  // file behind.
  LittleFS.remove(filename);
  if (!LittleFS.rename(tmpname, filename)) {
    // Clean up if rename fails
    LittleFS.remove(tmpname);
    return false;
  }
  // Update in-memory copy
  m_entries[index].doc.clear();
  // Copy doc into stored doc
  m_entries[index].doc.set(doc);
  m_entries[index].loaded = true;
  return true;
}
