#include "browserd/embedder_switches.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/no_destructor.h"
#include "base/strings/string_split.h"
#include "base/synchronization/lock.h"
#include "base/thread_annotations.h"
#include "content/public/common/content_switches.h"

namespace browserd {
namespace {

constexpr char kFallbackToSWIfGLES3NotSupported[] =
    "FallbackToSWIfGLES3NotSupported";
constexpr char kDisableWaylandGbmSurfaceless[] =
    "disable-wayland-gbm-surfaceless";
constexpr char kBrowserdForceGles2Context[] =
    "browserd-force-gles2-context";

struct SwitchPolicy {
  base::Lock lock;
  std::vector<EmbedderSwitch> switches GUARDED_BY(lock);
};

SwitchPolicy* GetPolicy() {
  static base::NoDestructor<SwitchPolicy> policy;
  return policy.get();
}

bool IsCommaMergedSwitch(std::string_view name) {
  return name == switches::kDisableFeatures ||
         name == switches::kEnableFeatures;
}

std::string MergeCommaSeparatedValues(std::string existing,
                                      const std::string& incoming) {
  if (existing.empty()) {
    return incoming;
  }
  if (incoming.empty()) {
    return existing;
  }

  std::vector<std::string> values = base::SplitString(
      existing, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (const std::string& value : base::SplitString(
           incoming, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (std::ranges::find(values, value) == values.end()) {
      values.push_back(value);
    }
  }

  std::string merged;
  for (const std::string& value : values) {
    if (!merged.empty()) {
      merged += ",";
    }
    merged += value;
  }
  return merged;
}

bool HasCommaSeparatedValue(const base::CommandLine& command_line,
                            std::string_view name,
                            std::string_view expected) {
  for (const std::string& value : base::SplitString(
           command_line.GetSwitchValueASCII(name), ",", base::TRIM_WHITESPACE,
           base::SPLIT_WANT_NONEMPTY)) {
    if (value == expected) {
      return true;
    }
  }
  return false;
}

void AppendSwitch(base::CommandLine* command_line,
                  const EmbedderSwitch& embedder_switch) {
  if (!command_line || embedder_switch.name.empty()) {
    return;
  }

  if (!embedder_switch.value.has_value()) {
    command_line->RemoveSwitch(embedder_switch.name);
    command_line->AppendSwitch(embedder_switch.name);
    return;
  }

  std::string value = embedder_switch.value.value();
  if (IsCommaMergedSwitch(embedder_switch.name)) {
    value = MergeCommaSeparatedValues(
        command_line->GetSwitchValueASCII(embedder_switch.name), value);
  } else {
    command_line->RemoveSwitch(embedder_switch.name);
  }
  command_line->AppendSwitchASCII(embedder_switch.name, value);
}

std::vector<EmbedderSwitch> GetSwitchesSnapshot() {
  SwitchPolicy* policy = GetPolicy();
  base::AutoLock lock(policy->lock);
  return policy->switches;
}

uint32_t ScopeForChildProcess(base::CommandLine* command_line) {
  if (!command_line) {
    return 0;
  }

  uint32_t scope = kSwitchScopeAllChildren;
  std::string process_type =
      command_line->GetSwitchValueASCII(::switches::kProcessType);

  if (process_type == ::switches::kGpuProcess) {
    scope |= kSwitchScopeGpuChild;
  } else if (process_type == ::switches::kRendererProcess) {
    scope |= kSwitchScopeRendererChild;
  }
  return scope;
}

void AppendSwitchesForScope(base::CommandLine* command_line, uint32_t scope) {
  if (!scope) {
    return;
  }

  for (const auto& embedder_switch : GetSwitchesSnapshot()) {
    if (embedder_switch.scope & scope) {
      AppendSwitch(command_line, embedder_switch);
    }
  }
}

void AppendDefaultGpuChildSwitches(base::CommandLine* command_line,
                                   uint32_t scope) {
  if (!command_line || !(scope & kSwitchScopeGpuChild)) {
    return;
  }
  AppendSwitch(command_line,
               EmbedderSwitch(kDisableWaylandGbmSurfaceless, std::nullopt,
                               kSwitchScopeGpuChild));
  AppendSwitch(command_line,
               EmbedderSwitch(kBrowserdForceGles2Context, std::nullopt,
                               kSwitchScopeGpuChild));

  if (HasCommaSeparatedValue(*command_line, switches::kEnableFeatures,
                             kFallbackToSWIfGLES3NotSupported)) {
    return;
  }

  AppendSwitch(command_line,
               EmbedderSwitch(switches::kDisableFeatures,
                               kFallbackToSWIfGLES3NotSupported,
                               kSwitchScopeGpuChild));
}

}  // namespace

EmbedderSwitch::EmbedderSwitch() = default;
EmbedderSwitch::EmbedderSwitch(std::string name_in,
                               std::optional<std::string> value_in,
                               uint32_t scope_in)
    : name(std::move(name_in)),
      value(std::move(value_in)),
      scope(scope_in) {}
EmbedderSwitch::~EmbedderSwitch() = default;
EmbedderSwitch::EmbedderSwitch(const EmbedderSwitch&) = default;
EmbedderSwitch& EmbedderSwitch::operator=(const EmbedderSwitch&) = default;
EmbedderSwitch::EmbedderSwitch(EmbedderSwitch&&) = default;
EmbedderSwitch& EmbedderSwitch::operator=(EmbedderSwitch&&) = default;

void SetEmbedderSwitches(std::vector<EmbedderSwitch> switches) {
  SwitchPolicy* policy = GetPolicy();
  base::AutoLock lock(policy->lock);
  policy->switches = std::move(switches);
}

void ClearEmbedderSwitches() {
  SetEmbedderSwitches({});
}

void AppendEmbedderSwitchesForBrowser(base::CommandLine* command_line) {
  AppendSwitchesForScope(command_line, kSwitchScopeBrowser);
}

void AppendEmbedderSwitchesForChild(base::CommandLine* command_line) {
  uint32_t scope = ScopeForChildProcess(command_line);
  AppendSwitchesForScope(command_line, scope);
  AppendDefaultGpuChildSwitches(command_line, scope);
}

}  // namespace browserd
