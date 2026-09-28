#include "WebUi.h"

#include "../AppConfig.h"
#include "../config/DeviceConfig.h"
#include "../web/web_assets.h"

namespace acorn {
namespace WebUi {

namespace {

constexpr const char* AP_HOST = "192.168.4.1";
constexpr const char* UI_URL = "http://192.168.4.1/";

// Operating-system connectivity checks. Anything other than their expected
// answer makes the phone open the portal page.
constexpr const char* PROBES[] = {
    "/generate_204",        "/gen_204",     "/hotspot-detect.html", "/library/test/success.html",
    "/connecttest.txt",     "/ncsi.txt",    "/redirect",            "/canonical.html",
    "/success.txt",
};

const WebAsset* find(const String& path) {
  for (size_t i = 0; i < WEB_ASSET_COUNT; i++) {
    if (path == WEB_ASSETS[i].path) return &WEB_ASSETS[i];
  }
  return nullptr;
}

bool isProbe(const String& path) {
  for (const char* p : PROBES) {
    if (path == p) return true;
  }
  return false;
}

// Requests for other host names arrive through the wildcard DNS.
bool isForeignHost(const String& host) {
  if (!host.length()) return false;
  const int colon = host.indexOf(':');
  const String name = colon < 0 ? host : host.substring(0, colon);
  return name != AP_HOST;
}

void redirect(WebServer& s) {
  s.sendHeader("Location", UI_URL);
  s.sendHeader("Cache-Control", "no-store");
  s.send(302, "text/plain", "");
}

}  // namespace

size_t assetCount() { return WEB_ASSET_COUNT; }

bool serve(WebServer& s) {
  if (s.method() != HTTP_GET) return false;
  String path = s.uri();
  if (isProbe(path) || isForeignHost(s.hostHeader())) {
    redirect(s);
    return true;
  }
  if (path == "/") path = "/index.html";
  const WebAsset* a = find(path);
  if (!a) {
    String msg = String(DeviceConfig::deviceId()) + " firmware " + ACORN_FIRMWARE_VERSION + "\nNot found: " +
                 s.uri() + "\n";
    s.send(404, "text/plain; charset=utf-8", msg);
    return true;
  }
  s.sendHeader("ETag", a->etag);
  s.sendHeader("Cache-Control", "no-cache");  // revalidate via ETag; the UI changes with firmware
  if (s.header("If-None-Match") == a->etag) {
    s.send(304);
    return true;
  }
  s.sendHeader("Content-Encoding", "gzip");
  s.send_P(200, a->mime, reinterpret_cast<const char*>(a->data), a->length);
  return true;
}

}  // namespace WebUi
}  // namespace acorn
