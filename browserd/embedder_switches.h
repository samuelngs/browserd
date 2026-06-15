#ifndef BROWSERD_EMBEDDER_SWITCHES_H_
#define BROWSERD_EMBEDDER_SWITCHES_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace base {
class CommandLine;
}  // namespace base

namespace browserd {

inline constexpr uint32_t kSwitchScopeBrowser = 1 << 0;
inline constexpr uint32_t kSwitchScopeGpuChild = 1 << 1;
inline constexpr uint32_t kSwitchScopeRendererChild = 1 << 2;
inline constexpr uint32_t kSwitchScopeAllChildren = 1 << 3;

struct EmbedderSwitch {
  EmbedderSwitch();
  EmbedderSwitch(std::string name_in,
                 std::optional<std::string> value_in,
                 uint32_t scope_in);
  ~EmbedderSwitch();
  EmbedderSwitch(const EmbedderSwitch&);
  EmbedderSwitch& operator=(const EmbedderSwitch&);
  EmbedderSwitch(EmbedderSwitch&&);
  EmbedderSwitch& operator=(EmbedderSwitch&&);

  std::string name;
  std::optional<std::string> value;
  uint32_t scope = 0;
};

void SetEmbedderSwitches(std::vector<EmbedderSwitch> switches);
void ClearEmbedderSwitches();
void AppendEmbedderSwitchesForBrowser(base::CommandLine* command_line);
void AppendEmbedderSwitchesForChild(base::CommandLine* command_line);

}  // namespace browserd

#endif  // BROWSERD_EMBEDDER_SWITCHES_H_
