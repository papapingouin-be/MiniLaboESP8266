// FileWriteService.cpp
//
// See header for description. This implementation performs a
// write-through to a temporary file and renames it to the target
// filename.  Only one task is processed per call to loop() to avoid
// blocking the main loop for too long.  Note: we do not log
// extensively here to keep the service generic.  The caller can
// instrument the queue via WebApi.

#include "FileWriteService.h"
#include <FS.h>
#include <LittleFS.h>

void FileWriteService::begin() {
  // Nothing to initialize. Ensure FS is mounted in main.cpp.
}

void FileWriteService::loop() {
  // If currently processing a task do nothing. In this simple
  // implementation we process one task per call and mark ourselves
  // busy until the task completes. A more advanced version could
  // implement state machine and yield() periodically.
  if (m_busy) {
    return;
  }
  if (m_queue.empty()) {
    return;
  }
  // Pop the next task
  Task task = m_queue.front();
  m_queue.pop();
  m_busy = true;
  // Build temporary filename. Use .tmp suffix appended to path.
  String tmpName = task.path + ".tmp";
  // Open temp file for writing. If it fails we silently drop the
  // request; the caller should handle logging elsewhere.
  File f = LittleFS.open(tmpName, "w");
  if (!f) {
    // Cannot open file; mark not busy and return
    m_busy = false;
    return;
  }
  // Write contents
  size_t written = f.print(task.contents);
  f.flush();
  f.close();
  if (written != task.contents.length()) {
    // Failed to write full contents; remove temp and exit
    LittleFS.remove(tmpName);
    m_busy = false;
    return;
  }
  // Remove original if exists and rename temp to target
  LittleFS.remove(task.path);
  if (!LittleFS.rename(tmpName, task.path)) {
    // Rename failed; remove temp
    LittleFS.remove(tmpName);
    m_busy = false;
    return;
  }
  // Task complete
  m_busy = false;
}

void FileWriteService::enqueue(const String &path, const String &contents) {
  Task t;
  t.path = path;
  t.contents = contents;
  m_queue.push(t);
}

size_t FileWriteService::pending() const {
  return m_queue.size() + (m_busy ? 1 : 0);
}