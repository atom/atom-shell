// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_ELECTRON_SPEECH_RECOGNITION_MANAGER_DELEGATE_H_
#define ELECTRON_SHELL_BROWSER_ELECTRON_SPEECH_RECOGNITION_MANAGER_DELEGATE_H_

#include <vector>

#include "content/public/browser/speech_recognition_event_listener.h"
#include "content/public/browser/speech_recognition_manager_delegate.h"

namespace electron {

class ElectronSpeechRecognitionManagerDelegate
    : public content::SpeechRecognitionManagerDelegate,
      public content::SpeechRecognitionEventListener {
 public:
  ElectronSpeechRecognitionManagerDelegate();
  ~ElectronSpeechRecognitionManagerDelegate() override;

  // disable copy
  ElectronSpeechRecognitionManagerDelegate(
      const ElectronSpeechRecognitionManagerDelegate&) = delete;
  ElectronSpeechRecognitionManagerDelegate& operator=(
      const ElectronSpeechRecognitionManagerDelegate&) = delete;

  // content::SpeechRecognitionEventListener:
  void OnRecognitionStart(int session_id) override;
  void OnAudioStart(int session_id) override;
  void OnSoundStart(int session_id) override;
  void OnSoundEnd(int session_id) override;
  void OnAudioEnd(int session_id) override;
  void OnRecognitionEnd(int session_id) override;
  void OnRecognitionResults(
      int session_id,
      const std::vector<media::mojom::WebSpeechRecognitionResultPtr>&) override;
  void OnRecognitionError(
      int session_id,
      const media::mojom::SpeechRecognitionError& error) override;
  void OnAudioLevelsChange(int session_id,
                           float volume,
                           float noise_volume) override;

  // content::SpeechRecognitionManagerDelegate:
  void CheckRecognitionIsAllowed(
      int session_id,
      base::OnceCallback<void(bool ask_user, bool is_allowed)> callback)
      override;
  content::SpeechRecognitionEventListener* GetEventListener() override;
  bool FilterProfanities(int render_process_id) override;
  void BindSpeechRecognitionContext(
      mojo::PendingReceiver<media::mojom::SpeechRecognitionContext> receiver)
      override;
};

}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_ELECTRON_SPEECH_RECOGNITION_MANAGER_DELEGATE_H_
