#include "browserd/chrome/platform_setup.h"

#include "build/build_config.h"

#if !BUILDFLAG(IS_MAC)

namespace browserd::chrome {

bool SetUpChromePlatform(int argc, const char** argv) {
  (void)argc;
  (void)argv;
  return true;
}

void ActivateChromeApplication() {}

}  // namespace browserd::chrome

#endif
