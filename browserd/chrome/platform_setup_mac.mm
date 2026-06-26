#include "browserd/chrome/platform_setup.h"

#include <ApplicationServices/ApplicationServices.h>
#include <unistd.h>

#import <Cocoa/Cocoa.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "base/apple/bundle_locations.h"
#include "base/apple/foundation_util.h"
#include "base/base_paths.h"
#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/common/chrome_constants.h"
#include "content/public/common/content_paths.h"

namespace browserd::chrome {
namespace {

constexpr char kBrowserdGuiAppReexecEnv[] = "BROWSERD_GUI_APP_REEXEC";
constexpr char kBrowserdGuiAppName[] = "browserd_gui.app";
constexpr char kBrowserdGuiExecutableName[] = "browserd_gui";

base::FilePath GetOutputDirectory() {
  base::FilePath executable_path;
  if (!base::PathService::Get(base::FILE_EXE, &executable_path)) {
    return base::FilePath();
  }
  return executable_path.DirName();
}

base::FilePath GetBuildOutputDirectory(const base::FilePath& output_dir) {
  if (output_dir.BaseName().value() == "MacOS" &&
      output_dir.DirName().BaseName().value() == "Contents" &&
      output_dir.DirName().DirName().Extension() == ".app") {
    return output_dir.DirName().DirName().DirName();
  }
  return output_dir;
}

base::FilePath GetBrowserdAppExecutablePath(
    const base::FilePath& build_output_dir) {
  return build_output_dir.AppendASCII(kBrowserdGuiAppName)
      .Append(FILE_PATH_LITERAL("Contents"))
      .Append(FILE_PATH_LITERAL("MacOS"))
      .AppendASCII(kBrowserdGuiExecutableName);
}

bool ReexecInAppBundleIfNeeded(int argc,
                               const char** argv,
                               const base::FilePath& build_output_dir) {
  if (base::apple::AmIBundled() || getenv(kBrowserdGuiAppReexecEnv)) {
    return true;
  }

  const base::FilePath app_executable =
      GetBrowserdAppExecutablePath(build_output_dir);
  if (!base::PathExists(app_executable)) {
    LOG(ERROR) << "Native --gui requires browserd.app at "
               << app_executable.value();
    return false;
  }

  std::vector<std::string> argv_storage;
  std::vector<char*> exec_argv;
  argv_storage.reserve(argc > 0 ? argc : 1);
  argv_storage.push_back(app_executable.value());
  if (argc > 1) {
    auto argv_span =
        UNSAFE_BUFFERS(base::span(argv, static_cast<size_t>(argc)));
    for (const char* arg : argv_span.subspan(size_t{1})) {
      argv_storage.emplace_back(arg);
    }
  }
  exec_argv.reserve(argv_storage.size() + 1);
  for (std::string& arg : argv_storage) {
    exec_argv.push_back(arg.data());
  }
  exec_argv.push_back(nullptr);

  setenv(kBrowserdGuiAppReexecEnv, "1", 1);
  execv(app_executable.value().c_str(), exec_argv.data());
  PLOG(ERROR) << "Failed to exec " << app_executable.value();
  return false;
}

base::FilePath GetChromeAppPath(const base::FilePath& output_dir) {
  return output_dir.Append(::chrome::kBrowserProcessExecutablePath)
      .DirName()
      .DirName()
      .DirName();
}

base::FilePath GetChromeFrameworkVersionPath(
    const base::FilePath& chrome_app_path) {
  return chrome_app_path.Append(FILE_PATH_LITERAL("Contents"))
      .Append(FILE_PATH_LITERAL("Frameworks"))
      .Append(::chrome::kFrameworkName)
      .Append(FILE_PATH_LITERAL("Versions"))
      .AppendASCII(::chrome::kChromeVersion);
}

base::FilePath GetChromeHelperPath(const base::FilePath& framework_path) {
  return framework_path.Append(FILE_PATH_LITERAL("Helpers"))
      .Append(::chrome::kHelperProcessExecutablePath);
}

}  // namespace

bool SetUpChromePlatform(int argc, const char** argv) {
  const base::FilePath output_dir = GetOutputDirectory();
  if (output_dir.empty()) {
    LOG(ERROR) << "Unable to locate browserd executable directory.";
    return false;
  }
  const base::FilePath build_output_dir = GetBuildOutputDirectory(output_dir);
  if (!ReexecInAppBundleIfNeeded(argc, argv, build_output_dir)) {
    return false;
  }

  const base::FilePath chrome_app_path = GetChromeAppPath(build_output_dir);
  const base::FilePath framework_path =
      GetChromeFrameworkVersionPath(chrome_app_path);
  const base::FilePath helper_path = GetChromeHelperPath(framework_path);

  if (!base::PathExists(chrome_app_path)) {
    LOG(ERROR) << "Native --gui requires the Chromium app bundle at "
               << chrome_app_path.value();
    return false;
  }
  if (!base::PathExists(framework_path)) {
    LOG(ERROR) << "Native --gui requires the Chromium framework at "
               << framework_path.value();
    return false;
  }
  if (!base::PathExists(helper_path)) {
    LOG(ERROR) << "Native --gui requires the Chromium helper at "
               << helper_path.value();
    return false;
  }

  base::apple::SetOverrideOuterBundlePath(chrome_app_path);
  base::apple::SetOverrideFrameworkBundlePath(framework_path);

  NSBundle* outer_bundle = base::apple::OuterBundle();
  base::apple::SetBaseBundleIDOverride(
      base::SysNSStringToUTF8(outer_bundle.bundleIdentifier));

  base::PathService::OverrideAndCreateIfNeeded(
      content::CHILD_PROCESS_EXE, helper_path, /*is_absolute=*/true,
      /*create=*/false);

  return true;
}

void ActivateChromeApplication() {
  @autoreleasepool {
    ProcessSerialNumber process;
    process.highLongOfPSN = 0;
    process.lowLongOfPSN = kCurrentProcess;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    TransformProcessType(&process, kProcessTransformToForegroundApplication);
    ShowHideProcess(&process, true);
    SetFrontProcessWithOptions(&process, kSetFrontProcessCausedByUser);
#pragma clang diagnostic pop

    NSApplication* app = NSApplication.sharedApplication;
    if (app.activationPolicy != NSApplicationActivationPolicyRegular) {
      [app setActivationPolicy:NSApplicationActivationPolicyRegular];
    }
    [app unhide:nil];
    [app activateIgnoringOtherApps:YES];
    [NSRunningApplication.currentApplication
        activateWithOptions:NSApplicationActivateIgnoringOtherApps |
                            NSApplicationActivateAllWindows];
  }
}

}  // namespace browserd::chrome
