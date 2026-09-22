#ifndef GUI2_BACKEND_WIFI_BACKEND_H
#define GUI2_BACKEND_WIFI_BACKEND_H

#include <string>
#include <vector>

namespace gui2_backend {

// What the scan reports about a network. Anything stronger than WPA2 needs a
// different key_mgmt, which is the only reason the page cares.
enum class wifi_security {
  OPEN,
  WPA,
  WPA2,
  WPA3,
};

struct wifi_network {
  std::string ssid;
  std::string bssid;
  int signal_dbm = 0;
  wifi_security security = wifi_security::OPEN;
};

enum class wifi_state {
  IDLE,
  SCANNING,
  CONNECTING,
  DONE,
  FAILED,
};

// Wi-Fi is an optional part of the build. When it is left out the recovery
// binary never hands the UI one of these, and every page that would show it
// checks for the null pointer instead of testing a build flag.
class wifi_backend {
 public:
  virtual ~wifi_backend() = default;

  // False when the device asks for the network to stay off.
  virtual bool available() = 0;

  // wpa_supplicant is an init service; the switch starts and stops it.
  virtual bool service_running() = 0;
  virtual bool set_service(bool running) = 0;

  // What the page shows under the list: every step the jobs take, the way the
  // legacy page fills its log box. Kept here rather than in the recovery
  // console so it stays with the page it belongs to.
  // The count covers every line ever logged. Only the newest are kept, and a
  // line that has been dropped reads back empty.
  virtual size_t log_count() = 0;
  virtual std::string log_line(size_t index) = 0;

  // The scan is asynchronous: the driver needs seconds to answer, so it runs on
  // a worker and the page watches state().
  virtual bool start_scan() = 0;
  virtual std::vector<wifi_network> networks() = 0;

  virtual bool start_connect(const std::string& ssid, wifi_security security,
                             const std::string& password) = 0;

  // The SSID currently associated, or empty. The status bar asks for this from
  // the UI loop, so it answers from a cache and refreshes it in the background.
  virtual std::string connected_ssid() = 0;

  // Both write what they find to the log above.
  virtual bool start_status() = 0;
  virtual bool start_test() = 0;

  virtual wifi_state state() = 0;
  virtual void acknowledge() = 0;
};

}  // namespace gui2_backend

#endif  // GUI2_BACKEND_WIFI_BACKEND_H
