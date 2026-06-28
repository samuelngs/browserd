#include "browserd/chrome/browserd_chrome_main.h"

#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/sampling_heap_profiler/poisson_allocation_sampler.h"
#include "browserd/chrome/browserd_chrome_main_delegate.h"
#include "browserd/chrome/platform_setup.h"
#include "content/public/app/content_main.h"

namespace browserd::chrome {
namespace {

void SetContentMainArgv(content::ContentMainParams* params,
                        int argc,
                        const char** argv,
                        base::CommandLine::StringVector* argv_storage,
                        std::vector<const char*>* argv_ptrs) {
  if (argc > 0 && argv) {
    params->argc = argc;
    params->argv = argv;
    return;
  }

  *argv_storage = base::CommandLine::ForCurrentProcess()->argv();
  argv_ptrs->reserve(argv_storage->size());
  for (const auto& arg : *argv_storage) {
    argv_ptrs->push_back(arg.c_str());
  }
  params->argc = static_cast<int>(argv_ptrs->size());
  params->argv = argv_ptrs->data();
}

}  // namespace

NO_STACK_PROTECTOR int RunChromeBrowserMain(
    int argc,
    const char** argv,
    RuntimeReadyCallback ready_callback) {
  if (!SetUpChromePlatform(argc, argv)) {
    return EXIT_FAILURE;
  }

  base::PoissonAllocationSampler::Init();

  BrowserdChromeMainDelegate delegate(std::move(ready_callback));
  content::ContentMainParams params(&delegate);
  base::CommandLine::StringVector argv_storage;
  std::vector<const char*> argv_ptrs;
  SetContentMainArgv(&params, argc, argv, &argv_storage, &argv_ptrs);
  return content::ContentMain(std::move(params));
}

}  // namespace browserd::chrome
