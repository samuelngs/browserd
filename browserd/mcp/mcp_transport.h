#ifndef BROWSERD_MCP_MCP_TRANSPORT_H_
#define BROWSERD_MCP_MCP_TRANSPORT_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/threading/thread.h"
#include "base/values.h"

namespace browserd {

class MCPTransport {
 public:
  using MessageCallback =
      base::RepeatingCallback<void(base::DictValue message)>;
  using CloseCallback = base::OnceClosure;

  MCPTransport();
  ~MCPTransport();

  MCPTransport(const MCPTransport&) = delete;
  MCPTransport& operator=(const MCPTransport&) = delete;

  void Start(scoped_refptr<base::SequencedTaskRunner> main_task_runner,
             MessageCallback message_callback,
             CloseCallback close_callback);
  void SendMessage(const base::DictValue& message);

 private:
  void ReadLoop();

  base::Thread reader_thread_;
  scoped_refptr<base::SequencedTaskRunner> main_task_runner_;
  MessageCallback message_callback_;
  CloseCallback close_callback_;
};

}  // namespace browserd

#endif  // BROWSERD_MCP_MCP_TRANSPORT_H_
