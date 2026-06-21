#ifndef BROWSERD_CORE_DEVTOOLS_EVALUATOR_H_
#define BROWSERD_CORE_DEVTOOLS_EVALUATOR_H_

#include <string>

#include "base/functional/callback.h"
#include "base/time/time.h"
#include "base/values.h"
#include "browserd/core/browser_result.h"

namespace content {
class WebContents;
}

namespace browserd {

struct DevToolsEvaluationResult {
  bool is_undefined = false;
  base::Value value;
};

using DevToolsEvaluationCallback =
    base::OnceCallback<void(BrowserResult<DevToolsEvaluationResult>)>;

void EvaluateWithDevTools(content::WebContents* web_contents,
                          std::string expression,
                          bool await_promise,
                          base::TimeDelta timeout,
                          DevToolsEvaluationCallback callback);

}  // namespace browserd

#endif  // BROWSERD_CORE_DEVTOOLS_EVALUATOR_H_
