// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_OSR_OSR_HOST_DISPLAY_CLIENT_H_
#define ELECTRON_SHELL_BROWSER_OSR_OSR_HOST_DISPLAY_CLIENT_H_

#include <memory>

#include "base/functional/callback.h"
#include "base/memory/shared_memory_mapping.h"
#include "components/viz/host/host_display_client.h"
#include "services/viz/privileged/mojom/compositing/layered_window_updater.mojom.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "ui/gfx/native_widget_types.h"

namespace electron {

typedef base::RepeatingCallback<void(const gfx::Rect&, const SkBitmap&)>
    OnPaintCallback;

class LayeredWindowUpdater : public viz::mojom::LayeredWindowUpdater {
 public:
  explicit LayeredWindowUpdater(
      mojo::PendingReceiver<viz::mojom::LayeredWindowUpdater> receiver,
      OnPaintCallback callback);
  ~LayeredWindowUpdater() override;

  // disable copy
  LayeredWindowUpdater(const LayeredWindowUpdater&) = delete;
  LayeredWindowUpdater& operator=(const LayeredWindowUpdater&) = delete;

  void SetActive(bool active);

  // viz::mojom::LayeredWindowUpdater implementation.
  void OnAllocatedSharedMemory(const gfx::Size& pixel_size,
                               base::UnsafeSharedMemoryRegion region) override;
  void Draw(const gfx::Rect& damage_rect, DrawCallback draw_callback) override;

 private:
  OnPaintCallback callback_;
  mojo::Receiver<viz::mojom::LayeredWindowUpdater> receiver_;
  std::unique_ptr<SkCanvas> canvas_;
  bool active_ = false;

#if !defined(WIN32)
  base::WritableSharedMemoryMapping shm_mapping_;
#endif
};

class OffScreenHostDisplayClient : public viz::HostDisplayClient {
 public:
  explicit OffScreenHostDisplayClient(gfx::AcceleratedWidget widget,
                                      OnPaintCallback callback);
  ~OffScreenHostDisplayClient() override;

  // disable copy
  OffScreenHostDisplayClient(const OffScreenHostDisplayClient&) = delete;
  OffScreenHostDisplayClient& operator=(const OffScreenHostDisplayClient&) =
      delete;

  void SetActive(bool active);

 private:
  // viz::HostDisplayClient
#if BUILDFLAG(IS_MAC)
  void OnDisplayReceivedCALayerParams(
      const gfx::CALayerParams& ca_layer_params) override;
#endif

  void CreateLayeredWindowUpdater(
      mojo::PendingReceiver<viz::mojom::LayeredWindowUpdater> receiver)
      override;

#if BUILDFLAG(IS_LINUX) && !BUILDFLAG(IS_CHROMEOS)
  void DidCompleteSwapWithNewSize(const gfx::Size& size) override;
#endif

  std::unique_ptr<LayeredWindowUpdater> layered_window_updater_;
  OnPaintCallback callback_;
  bool active_ = false;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_OSR_OSR_HOST_DISPLAY_CLIENT_H_
