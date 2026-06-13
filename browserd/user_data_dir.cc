#include "browserd/user_data_dir.h"

#include <memory>

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "browserd/switches.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
#include "base/nix/xdg_util.h"
#endif

namespace browserd {
namespace {

bool EnsureDirectory(base::FilePath* path) {
  if (path->empty()) {
    return false;
  }

  if (!base::DirectoryExists(*path) && !base::CreateDirectory(*path)) {
    LOG(ERROR) << "Unable to create user data directory: " << path->value();
    return false;
  }

  if (!path->IsAbsolute()) {
    *path = base::MakeAbsoluteFilePath(*path);
  }

  return !path->empty();
}

bool GetCommandLineUserDataDir(base::FilePath* path, bool* has_switch) {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  *has_switch = command_line->HasSwitch(switches::kUserDataDir);
  if (!*has_switch) {
    return true;
  }

  *path = command_line->GetSwitchValuePath(switches::kUserDataDir);
  if (path->empty()) {
    LOG(ERROR) << "--" << switches::kUserDataDir << " requires a path";
    return false;
  }

  return EnsureDirectory(path);
}

bool GetDefaultGuiUserDataDir(base::FilePath* path) {
#if BUILDFLAG(IS_WIN)
  if (!base::PathService::Get(base::DIR_LOCAL_APP_DATA, path)) {
    return false;
  }
  *path = path->Append(FILE_PATH_LITERAL("browserd"));
#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  std::unique_ptr<base::Environment> environment(base::Environment::Create());
  *path = base::FilePath(base::nix::GetXDGDirectory(
              environment.get(), base::nix::kXdgConfigHomeEnvVar,
              base::nix::kDotConfigDir))
              .Append("browserd");
#elif BUILDFLAG(IS_APPLE)
  if (!base::PathService::Get(base::DIR_APP_DATA, path)) {
    return false;
  }
  *path = path->Append("browserd");
#else
  if (!base::PathService::Get(base::DIR_HOME, path)) {
    return false;
  }
  *path = path->Append(FILE_PATH_LITERAL(".browserd"));
#endif

  return EnsureDirectory(path);
}

}  // namespace

bool GetGuiUserDataDir(base::FilePath* path) {
  bool has_switch = false;
  if (!GetCommandLineUserDataDir(path, &has_switch)) {
    return false;
  }
  if (has_switch) {
    return true;
  }
  return GetDefaultGuiUserDataDir(path);
}

bool GetHeadlessUserDataDir(base::FilePath* path) {
  bool has_switch = false;
  if (!GetCommandLineUserDataDir(path, &has_switch)) {
    return false;
  }
  if (has_switch) {
    return true;
  }
  return base::CreateNewTempDirectory(FILE_PATH_LITERAL("browserd"), path);
}

}  // namespace browserd
