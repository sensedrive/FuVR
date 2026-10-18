// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <vector>

#include "fuvr/frame_sink.hpp"
#include "fuvr/runtime.hpp"

namespace fr = fuvr::runtime;

namespace fuvr::runtime {
extern XrResult xrCreateInstance_impl(const XrInstanceCreateInfo*,
                                       XrInstance*) noexcept;
extern XrResult xrDestroyInstance_impl(XrInstance) noexcept;
extern XrResult xrCreateSession_impl(XrInstance, const XrSessionCreateInfo*,
                                      XrSession*) noexcept;
extern XrResult xrDestroySession_impl(XrSession) noexcept;
extern XrResult xrCreateSwapchain_impl(XrSession,
                                        const XrSwapchainCreateInfo*,
                                        XrSwapchain*) noexcept;
extern XrResult xrAcquireSwapchainImage_impl(XrSwapchain,
                                              const XrSwapchainImageAcquireInfo*,
                                              uint32_t*) noexcept;
extern XrResult xrReleaseSwapchainImage_impl(
    XrSwapchain, const XrSwapchainImageReleaseInfo*) noexcept;
extern XrResult xrEndFrame_impl(XrSession, const XrFrameEndInfo*) noexcept;
extern XrResult xrBeginFrame_impl(XrSession, const XrFrameBeginInfo*) noexcept;
}

namespace {

class CapturingSink final : public fr::FrameSink {
 public:
  std::vector<fr::SubmittedFrame> frames;
  void submit(const fr::SubmittedFrame& f) noexcept override {
    frames.push_back(f);
  }
};

XrSwapchain createSc(XrSession sess, uint32_t w, uint32_t h,
                     uint32_t arraySize = 1) {
  XrSwapchainCreateInfo info{};
  info.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
  info.width = w;
  info.height = h;
  info.format = 37;
  info.arraySize = arraySize;
  info.faceCount = 1;
  info.mipCount = 1;
  info.sampleCount = 1;
  XrSwapchain sc = XR_NULL_HANDLE;
  fr::xrCreateSwapchain_impl(sess, &info, &sc);
  return sc;
}

}  // namespace

TEST(EndFrame, WalksAllProjectionAndQuadLayers) {
  XrInstanceCreateInfo ici{};
  ici.type = XR_TYPE_INSTANCE_CREATE_INFO;
  XrInstance inst = XR_NULL_HANDLE;
  ASSERT_EQ(fr::xrCreateInstance_impl(&ici, &inst), XR_SUCCESS);
  XrSessionCreateInfo sci{};
  sci.type = XR_TYPE_SESSION_CREATE_INFO;
  XrSession sess = XR_NULL_HANDLE;
  ASSERT_EQ(fr::xrCreateSession_impl(inst, &sci, &sess), XR_SUCCESS);

  fr::Session* s = fr::lookupSession(sess);
  ASSERT_NE(s, nullptr);
  auto sink = std::make_unique<CapturingSink>();
  CapturingSink* raw = sink.get();
  s->frameSink = std::move(sink);

  XrSwapchain projSc = createSc(sess, 256, 128);
  XrSwapchain quadSc = createSc(sess, 64, 64);
  ASSERT_NE(projSc, XR_NULL_HANDLE);
  ASSERT_NE(quadSc, XR_NULL_HANDLE);

  uint32_t idx = 0;
  fr::xrAcquireSwapchainImage_impl(projSc, nullptr, &idx);
  fr::xrReleaseSwapchainImage_impl(projSc, nullptr);
  fr::xrAcquireSwapchainImage_impl(quadSc, nullptr, &idx);
  fr::xrReleaseSwapchainImage_impl(quadSc, nullptr);
  fr::xrBeginFrame_impl(sess, nullptr);

  XrCompositionLayerProjectionView views[2]{};
  for (auto& v : views) {
    v.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
    v.subImage.swapchain = projSc;
    v.subImage.imageRect.extent.width = 256;
    v.subImage.imageRect.extent.height = 128;
    v.pose.orientation.w = 1.0f;
  }
  XrCompositionLayerProjection proj{};
  proj.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
  proj.viewCount = 2;
  proj.views = views;

  XrCompositionLayerQuad quad{};
  quad.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
  quad.subImage.swapchain = quadSc;
  quad.size.width = 1.0f;
  quad.size.height = 1.0f;
  quad.pose.orientation.w = 1.0f;

  const XrCompositionLayerBaseHeader* layers[2] = {
      reinterpret_cast<const XrCompositionLayerBaseHeader*>(&proj),
      reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad),
  };
  XrFrameEndInfo end{};
  end.type = XR_TYPE_FRAME_END_INFO;
  end.displayTime = 1000;
  end.layerCount = 2;
  end.layers = layers;
  ASSERT_EQ(fr::xrEndFrame_impl(sess, &end), XR_SUCCESS);

  ASSERT_EQ(raw->frames.size(), 1u);
  const auto& f = raw->frames.front();
  EXPECT_NE(f.ioSurface, nullptr);
  EXPECT_EQ(f.extraLayers.size(), 1u);
  EXPECT_NE(f.extraLayers.front(), nullptr);
  EXPECT_NE(f.ioSurface, f.extraLayers.front());

  fr::xrDestroySession_impl(sess);
  fr::xrDestroyInstance_impl(inst);
}

// Multiview apps (Godot) submit one arraySize=2 swapchain and address the
// eyes via imageArrayIndex; each eye must resolve to its own slice.
TEST(EndFrame, ArraySwapchainResolvesOneSlicePerEye) {
  XrInstanceCreateInfo ici{};
  ici.type = XR_TYPE_INSTANCE_CREATE_INFO;
  XrInstance inst = XR_NULL_HANDLE;
  ASSERT_EQ(fr::xrCreateInstance_impl(&ici, &inst), XR_SUCCESS);
  XrSessionCreateInfo sci{};
  sci.type = XR_TYPE_SESSION_CREATE_INFO;
  XrSession sess = XR_NULL_HANDLE;
  ASSERT_EQ(fr::xrCreateSession_impl(inst, &sci, &sess), XR_SUCCESS);

  fr::Session* s = fr::lookupSession(sess);
  ASSERT_NE(s, nullptr);
  auto sink = std::make_unique<CapturingSink>();
  CapturingSink* raw = sink.get();
  s->frameSink = std::move(sink);

  XrSwapchain sc = createSc(sess, 256, 128, 2);
  ASSERT_NE(sc, XR_NULL_HANDLE);
  uint32_t idx = 0;
  fr::xrAcquireSwapchainImage_impl(sc, nullptr, &idx);
  fr::xrReleaseSwapchainImage_impl(sc, nullptr);
  fr::xrBeginFrame_impl(sess, nullptr);

  XrCompositionLayerProjectionView views[2]{};
  for (uint32_t eye = 0; eye < 2; ++eye) {
    views[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
    views[eye].subImage.swapchain = sc;
    views[eye].subImage.imageArrayIndex = eye;
    views[eye].subImage.imageRect.extent.width = 256;
    views[eye].subImage.imageRect.extent.height = 128;
    views[eye].pose.orientation.w = 1.0f;
  }
  XrCompositionLayerProjection proj{};
  proj.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
  proj.viewCount = 2;
  proj.views = views;
  const XrCompositionLayerBaseHeader* layers[1] = {
      reinterpret_cast<const XrCompositionLayerBaseHeader*>(&proj)};
  XrFrameEndInfo end{};
  end.type = XR_TYPE_FRAME_END_INFO;
  end.displayTime = 1000;
  end.layerCount = 1;
  end.layers = layers;
  ASSERT_EQ(fr::xrEndFrame_impl(sess, &end), XR_SUCCESS);

  ASSERT_EQ(raw->frames.size(), 1u);
  const auto& f = raw->frames.front();
  EXPECT_NE(f.ioSurface, nullptr);
  EXPECT_EQ(f.width, 512u);
  EXPECT_NE(f.leftMetalTexture, nullptr);
  EXPECT_NE(f.rightMetalTexture, nullptr);
  EXPECT_NE(f.leftMetalTexture, f.rightMetalTexture);

  fr::xrDestroySession_impl(sess);
  fr::xrDestroyInstance_impl(inst);
}
