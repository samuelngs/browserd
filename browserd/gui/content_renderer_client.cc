#include "browserd/gui/content_renderer_client.h"

#include "base/logging.h"
#include "content/public/renderer/render_frame.h"

namespace browserd::gui {

ContentRendererClient::ContentRendererClient() = default;
ContentRendererClient::~ContentRendererClient() = default;

void ContentRendererClient::RenderFrameCreated(
    content::RenderFrame* render_frame) {
  LOG(INFO) << "browserd renderer RenderFrameCreated frame=" << render_frame;
}

}  // namespace browserd::gui
