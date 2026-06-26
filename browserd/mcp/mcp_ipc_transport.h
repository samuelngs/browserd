#ifndef BROWSERD_MCP_MCP_IPC_TRANSPORT_H_
#define BROWSERD_MCP_MCP_IPC_TRANSPORT_H_

#include <cstdint>
#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/synchronization/lock.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/thread.h"
#include "base/values.h"
#include "browserd/mcp/mcp_transport.h"
#include "build/build_config.h"

namespace base {
class WaitableEvent;
}  // namespace base

namespace browserd {

class MCPIPCTransport {
 public:
  MCPIPCTransport();
  ~MCPIPCTransport();

  MCPIPCTransport(const MCPIPCTransport&) = delete;
  MCPIPCTransport& operator=(const MCPIPCTransport&) = delete;

  bool Start(scoped_refptr<base::SequencedTaskRunner> main_task_runner,
             const std::string& path,
             MCPMessageCallback message_callback);

 private:
  void RunLoop(std::string path, bool* success, base::WaitableEvent* started);
  void RequestStopLocked();
  bool IsStopping();
  void DispatchLine(std::string line, uint64_t client_generation);
  void SendResponse(uint64_t client_generation,
                    std::optional<base::DictValue> response);
  void SendSerializedResponse(uint64_t client_generation,
                              const std::string& serialized);

  base::Thread ipc_thread_;
  scoped_refptr<base::SequencedTaskRunner> main_task_runner_;
  scoped_refptr<base::SequencedTaskRunner> ipc_task_runner_;
  MCPMessageCallback message_callback_;

  base::Lock lock_;
  bool stopping_ = false;
  uint64_t active_client_generation_ = 0;

#if BUILDFLAG(IS_WIN)
  void* active_client_handle_ = nullptr;
#else
  int listener_fd_ = -1;
  int active_client_fd_ = -1;
  base::FilePath bound_socket_path_;
#endif
};

}  // namespace browserd

#endif  // BROWSERD_MCP_MCP_IPC_TRANSPORT_H_
