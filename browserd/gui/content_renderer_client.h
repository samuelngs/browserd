#ifndef BROWSERD_GUI_CONTENT_RENDERER_CLIENT_H_
#define BROWSERD_GUI_CONTENT_RENDERER_CLIENT_H_

#include "content/public/renderer/content_renderer_client.h"

namespace browserd::gui {

class ContentRendererClient : public content::ContentRendererClient {
 public:
  ContentRendererClient();
  ~ContentRendererClient() override;

  ContentRendererClient(const ContentRendererClient&) = delete;
  ContentRendererClient& operator=(const ContentRendererClient&) = delete;

 private:
  void RenderFrameCreated(content::RenderFrame* render_frame) override;
};

}  // namespace browserd::gui

#endif  // BROWSERD_GUI_CONTENT_RENDERER_CLIENT_H_
