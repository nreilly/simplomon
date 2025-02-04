#include "fmt/format.h"
#include "fmt/ranges.h"
#include <map>
#include <curl/curl.h>
#include "notifiers.hh"
#include "nlohmann/json.hpp"
#include <unistd.h>
#include <time.h>
#include "minicurl.hh"
#include <thread>
#include <mutex>
#include "simplomon.hh"
#include "sol/sol.hpp"

using namespace std;


vector<CheckerNotifierCombo> g_checkers;

vector<std::shared_ptr<Notifier>> g_notifiers;

int main(int argc, char **argv)
{
  initLua();
  try {
    if(auto ptr = getenv("SIMPLOMON_CONFIG_URL")) {
      MiniCurl mc;
      fmt::print("Getting config from the network '{}'\n", ptr);
      string script = mc.getURL(ptr);
      g_lua.safe_script(script);
    }
    else if(argc > 1) {
      string src = argv[1];
      if(src.find("https://")==0) {
        MiniCurl mc;
        fmt::print("Getting config from '{}'\n", src);
        string script = mc.getURL(src);
        g_lua.safe_script(script);
      }
      else
        g_lua.safe_script_file(src);
    }
    else {
      fmt::print("Getting config from '{}'\n", "simplomon.conf");
      g_lua.safe_script_file("simplomon.conf");
    }

  }
  catch(sol::error& e) {
    fmt::print("Error parsing configuration: {}\n", e.what());
    return EXIT_FAILURE;
  }

  if(g_notifiers.empty()) {
    fmt::print("Did not configure a notifier, can't notify anything\n");
  }

  for(;;) {
    for(auto &c : g_checkers) {
      string reason;
      try {
        CheckResult cr = c.checker->perform();
        reason = cr.d_reason;
      }
      catch(exception& e) {
        reason = "Exception caught: "+string(e.what());
      }
      catch(...) {
        reason = "Unknown exception caught";
      }

      if(!reason.empty())
        c.checker->d_af.reportAlert();

      // we could still be in an alert, even though the check was ok now
      bool inAlert = c.checker->d_af.shouldAlert(c.checker->d_minalerts, c.checker->d_alertwindow);
      if(!inAlert) {// muted
        if(!reason.empty())
          fmt::print("Muting an alert since it does not yet meet threshold. The alert: {}\n", reason);
        reason="";
      }
      else if(reason.empty()) {
        fmt::print("Continuing alert, despite test now saying it is ok\n");
        reason = c.checker->d_alertedreason; // maintain alert
      }
      
      if(reason != c.checker->d_alertedreason) { // change of state
        string msg;
        if(!reason.empty()) 
          msg=fmt::format("{}\n", reason);
        else
          msg=fmt::format("🥳 The following alert is resolved: {}\n", c.checker->d_alertedreason);
        c.checker->d_alertedreason = reason;

        for(const auto & n : c.notifiers) {
          try {
            n->alert(msg);
          }
          catch(exception& e) {
            fmt::print("Failed to send notification: {}\n", e.what());
          }
        }
        fmt::print("Sent out notification: {}\n", msg);
      }
      else if(!reason.empty()) {
        fmt::print("Alert '{}' still active, but reported already\n", reason);
      }
    }
    sleep(60);
  }
}
