#ifndef BROWSERD_MCP_MCP_TRANSPORT_H_
#define BROWSERD_MCP_MCP_TRANSPORT_H_

#include <string>
#include <optional>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/thread.h"
#include "base/values.h"

namespace browserd {

using MCPResponseCallback =
    base::OnceCallback<void(std::optional<base::DictValue> response)>;
using MCPMessageCallback =
    base::RepeatingCallback<void(base::DictValue message,
                                 MCPResponseCallback response_callback)>;

std::optional<base::DictValue> ParseMCPTransportLine(const std::string& line);
std::optional<std::string> SerializeMCPTransportMessage(
    const base::DictValue& message);

class MCPStdioTransport {
 public:
  using CloseCallback = base::OnceClosure;

  MCPStdioTransport();
  ~MCPStdioTransport();

  MCPStdioTransport(const MCPStdioTransport&) = delete;
  MCPStdioTransport& operator=(const MCPStdioTransport&) = delete;

  void Start(scoped_refptr<base::SequencedTaskRunner> main_task_runner,
             MCPMessageCallback message_callback,
             CloseCallback close_callback);

 private:
  void ReadLoop();
  void SendResponse(std::optional<base::DictValue> response);
  void SendMessage(const base::DictValue& message);

  base::Thread reader_thread_;
  scoped_refptr<base::SequencedTaskRunner> main_task_runner_;
  MCPMessageCallback message_callback_;
  CloseCallback close_callback_;
};

}  // namespace browserd

#endif  // BROWSERD_MCP_MCP_TRANSPORT_H_
