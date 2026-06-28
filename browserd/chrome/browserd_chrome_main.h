#ifndef BROWSERD_CHROME_BROWSERD_CHROME_MAIN_H_
#define BROWSERD_CHROME_BROWSERD_CHROME_MAIN_H_

#include <memory>

#include "base/compiler_specific.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/sequenced_task_runner.h"

namespace browserd {
class BrowserRuntime;
}

namespace browserd::chrome {

using RuntimeReadyCallback =
    base::OnceCallback<void(std::unique_ptr<BrowserRuntime>,
                            scoped_refptr<base::SequencedTaskRunner>)>;

NO_STACK_PROTECTOR int RunChromeBrowserMain(
    int argc,
    const char** argv,
    RuntimeReadyCallback ready_callback);

}  // namespace browserd::chrome

#endif  // BROWSERD_CHROME_BROWSERD_CHROME_MAIN_H_
