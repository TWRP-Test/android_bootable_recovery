#ifndef GUI2_BACKEND_BACKGROUND_BACKEND_H
#define GUI2_BACKEND_BACKGROUND_BACKEND_H

namespace gui2_backend {

// The work the legacy page loop does between frames: the pipe of the "twrp"
// command line tool, storage hotplug, and the async /data size scan.
class background_backend {
 public:
  virtual ~background_backend() = default;

  virtual void start() = 0;
  // One pass; runs on the UI thread every loop iteration.
  virtual void poll() = 0;
  // A command from the "twrp" tool, or an adb backup or restore, is running in
  // the background.
  virtual bool command_running() = 0;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_BACKGROUND_BACKEND_H
