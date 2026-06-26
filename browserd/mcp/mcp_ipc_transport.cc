#include "browserd/mcp/mcp_ipc_transport.h"

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/message_loop/message_pump_type.h"
#include "base/synchronization/waitable_event.h"

namespace browserd {

MCPIPCTransport::MCPIPCTransport() : ipc_thread_("MCPIPCServer") {}

MCPIPCTransport::~MCPIPCTransport() {
  {
    base::AutoLock lock(lock_);
    stopping_ = true;
    RequestStopLocked();
  }

  if (ipc_thread_.IsRunning()) {
    ipc_thread_.Stop();
  }
}

bool MCPIPCTransport::Start(
    scoped_refptr<base::SequencedTaskRunner> main_task_runner,
    const std::string& path,
    MCPMessageCallback message_callback) {
  if (path.empty()) {
    LOG(ERROR) << "Invalid empty MCP IPC path";
    return false;
  }

  main_task_runner_ = std::move(main_task_runner);
  message_callback_ = std::move(message_callback);

  base::Thread::Options options;
  options.message_pump_type = base::MessagePumpType::IO;
  if (!ipc_thread_.StartWithOptions(std::move(options))) {
    LOG(ERROR) << "Failed to start MCP IPC server thread";
    return false;
  }
  ipc_task_runner_ = ipc_thread_.task_runner();

  bool success = false;
  base::WaitableEvent started;
  ipc_task_runner_->PostTask(FROM_HERE,
                             base::BindOnce(&MCPIPCTransport::RunLoop,
                                            base::Unretained(this), path,
                                            &success, &started));
  started.Wait();
  if (!success) {
    {
      base::AutoLock lock(lock_);
      stopping_ = true;
      RequestStopLocked();
    }
    ipc_thread_.Stop();
    ipc_task_runner_ = nullptr;
  }
  return success;
}

bool MCPIPCTransport::IsStopping() {
  base::AutoLock lock(lock_);
  return stopping_;
}

void MCPIPCTransport::DispatchLine(std::string line,
                                   uint64_t client_generation) {
  auto parsed = ParseMCPTransportLine(line);
  if (!parsed.has_value()) {
    return;
  }

  main_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(message_callback_, std::move(parsed.value()),
                                base::BindOnce(&MCPIPCTransport::SendResponse,
                                               base::Unretained(this),
                                               client_generation)));
}

void MCPIPCTransport::SendResponse(uint64_t client_generation,
                                   std::optional<base::DictValue> response) {
  if (!response.has_value()) {
    return;
  }

  std::optional<std::string> serialized =
      SerializeMCPTransportMessage(response.value());
  if (!serialized.has_value()) {
    return;
  }

  SendSerializedResponse(client_generation, serialized.value());
}

}  // namespace browserd
