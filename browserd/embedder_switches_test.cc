#include "browserd/embedder_switches.h"

#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "browserd/gui/content_browser_client.h"
#include "browserd/switches.h"
#include "content/public/common/content_switches.h"

namespace {

bool ExpectEqual(const std::string& label,
                 const std::string& actual,
                 const std::string& expected) {
  if (actual == expected) {
    return true;
  }
  std::cerr << label << ": expected `" << expected << "`, got `" << actual
            << "`\n";
  return false;
}

bool ExpectHasSwitch(const std::string& label,
                     const base::CommandLine& command_line,
                     const std::string& name) {
  if (command_line.HasSwitch(name)) {
    return true;
  }
  std::cerr << label << ": expected switch `" << name << "`\n";
  return false;
}

bool ExpectNoSwitch(const std::string& label,
                    const base::CommandLine& command_line,
                    const std::string& name) {
  if (!command_line.HasSwitch(name)) {
    return true;
  }
  std::cerr << label << ": unexpected switch `" << name << "`\n";
  return false;
}

bool TestUnknownChildReceivesAllChildrenSwitch() {
  browserd::SetEmbedderSwitches({
      browserd::EmbedderSwitch("all-child", std::nullopt,
                               browserd::kSwitchScopeAllChildren),
  });

  base::CommandLine child(base::CommandLine::NO_PROGRAM);
  browserd::AppendEmbedderSwitchesForChild(&child);

  bool ok = ExpectHasSwitch("unknown child all-child", child, "all-child");
  browserd::ClearEmbedderSwitches();
  return ok;
}

bool TestUnknownChildDoesNotReceiveGpuOnlySwitch() {
  browserd::SetEmbedderSwitches({
      browserd::EmbedderSwitch("gpu-only", std::nullopt,
                               browserd::kSwitchScopeGpuChild),
  });

  base::CommandLine child(base::CommandLine::NO_PROGRAM);
  browserd::AppendEmbedderSwitchesForChild(&child);

  bool ok = ExpectNoSwitch("unknown child gpu-only", child, "gpu-only");
  browserd::ClearEmbedderSwitches();
  return ok;
}

bool TestContentBrowserClientForwardsGpuChildSwitch() {
  browserd::SetEmbedderSwitches({
      browserd::EmbedderSwitch("all-child", std::nullopt,
                               browserd::kSwitchScopeAllChildren),
      browserd::EmbedderSwitch(
          switches::kDisableFeatures,
          "FallbackToSWIfGLES3NotSupported",
          browserd::kSwitchScopeGpuChild),
  });

  base::CommandLine child(base::CommandLine::NO_PROGRAM);
  child.AppendSwitchASCII(::switches::kProcessType, ::switches::kGpuProcess);
  browserd::gui::ContentBrowserClient client;
  client.AppendExtraCommandLineSwitches(&child, 0);

  bool ok = ExpectEqual(
      "gpu child disable-features",
      child.GetSwitchValueASCII(switches::kDisableFeatures),
      "FallbackToSWIfGLES3NotSupported");
  if (!child.HasSwitch(browserd::switches::kGui)) {
    std::cerr << "gui switch was not appended to gpu child\n";
    ok = false;
  }
  ok = ExpectHasSwitch("gpu child all-child", child, "all-child") && ok;
  browserd::ClearEmbedderSwitches();
  return ok;
}

bool TestRendererReceivesAllChildrenButNotGpuOnlySwitch() {
  browserd::SetEmbedderSwitches({
      browserd::EmbedderSwitch("all-child", std::nullopt,
                               browserd::kSwitchScopeAllChildren),
      browserd::EmbedderSwitch(
          switches::kDisableFeatures,
          "FallbackToSWIfGLES3NotSupported",
          browserd::kSwitchScopeGpuChild),
  });

  base::CommandLine child(base::CommandLine::NO_PROGRAM);
  child.AppendSwitchASCII(::switches::kProcessType,
                          ::switches::kRendererProcess);
  browserd::AppendEmbedderSwitchesForChild(&child);

  bool ok = ExpectEqual("renderer disable-features",
                        child.GetSwitchValueASCII(switches::kDisableFeatures),
                        "");
  ok = ExpectHasSwitch("renderer all-child", child, "all-child") && ok;
  browserd::ClearEmbedderSwitches();
  return ok;
}

bool TestAllChildrenAndFeatureMerging() {
  browserd::SetEmbedderSwitches({
      browserd::EmbedderSwitch(
          switches::kDisableFeatures,
          "FallbackToSWIfGLES3NotSupported,DefaultANGLEVulkan",
          browserd::kSwitchScopeAllChildren),
  });

  base::CommandLine child(base::CommandLine::NO_PROGRAM);
  child.AppendSwitchASCII(::switches::kProcessType, ::switches::kGpuProcess);
  child.AppendSwitchASCII(switches::kDisableFeatures, "Vulkan");
  browserd::AppendEmbedderSwitchesForChild(&child);

  bool ok = ExpectEqual(
      "merged disable-features",
      child.GetSwitchValueASCII(switches::kDisableFeatures),
      "Vulkan,FallbackToSWIfGLES3NotSupported,DefaultANGLEVulkan");
  browserd::ClearEmbedderSwitches();
  return ok;
}

bool TestBrowserScope() {
  browserd::SetEmbedderSwitches({
      browserd::EmbedderSwitch("browser-only", std::nullopt,
                               browserd::kSwitchScopeBrowser),
  });

  base::CommandLine browser(base::CommandLine::NO_PROGRAM);
  browserd::AppendEmbedderSwitchesForBrowser(&browser);

  bool ok = browser.HasSwitch("browser-only");
  if (!ok) {
    std::cerr << "browser-only switch was not appended\n";
  }
  browserd::ClearEmbedderSwitches();
  return ok;
}

}  // namespace

int main() {
  if (!TestUnknownChildReceivesAllChildrenSwitch() ||
      !TestUnknownChildDoesNotReceiveGpuOnlySwitch() ||
      !TestContentBrowserClientForwardsGpuChildSwitch() ||
      !TestRendererReceivesAllChildrenButNotGpuOnlySwitch() ||
      !TestAllChildrenAndFeatureMerging() || !TestBrowserScope()) {
    return 1;
  }
  return 0;
}
