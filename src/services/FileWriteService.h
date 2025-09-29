// FileWriteService.h
//
// This service provides a simple queued file writing mechanism.  It
// accepts requests to persist arbitrary strings to a given path on the
// LittleFS filesystem and processes them one at a time in the main
// loop.  By deferring writes outside of HTTP request handlers and
// executing them serially, we avoid long blocking operations that
// could trigger the watchdog or cause reboots.  The service writes to
// a temporary file and renames it to ensure atomicity.  Pending
// entries can be queried via a web API to aid debugging.

#pragma once

#include <Arduino.h>
#include <queue>

class FileWriteService {
public:
  // Initialize the service. Currently no state to initialize.
  void begin();
  // Process the queue. Should be called regularly from loop(). Each
  // call writes at most one file to avoid blocking too long.
  void loop();
  // Add a new write request. The contents string will be written to
  // the specified path. If a previous request for the same path is
  // pending it is not deduplicated – both writes will happen in
  // order. The caller must ensure the contents persist until the
  // write occurs (String is copied by value here).
  void enqueue(const String &path, const String &contents);
  // Number of pending write requests.
  size_t pending() const;

private:
  struct Task {
    String path;
    String contents;
  };
  std::queue<Task> m_queue;
  bool m_busy{false};
};