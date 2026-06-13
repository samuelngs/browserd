#include "browserd/gui/content_renderer_client.h"

#include "content/public/renderer/render_frame.h"
#include "headless/lib/renderer/headless_chrome_object_injector.h"

namespace browserd::gui {

ContentRendererClient::ContentRendererClient() = default;
ContentRendererClient::~ContentRendererClient() = default;

void ContentRendererClient::RenderFrameCreated(
    content::RenderFrame* render_frame) {
  new headless::HeadlessChromeObjectInjector(render_frame);
}

}  // namespace browserd::gui
