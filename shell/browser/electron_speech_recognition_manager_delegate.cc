// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/electron_speech_recognition_manager_delegate.h"

#include <utility>

#include "base/functional/callback.h"

namespace electron {

ElectronSpeechRecognitionManagerDelegate::
    ElectronSpeechRecognitionManagerDelegate() = default;

ElectronSpeechRecognitionManagerDelegate::
    ~ElectronSpeechRecognitionManagerDelegate() = default;

void ElectronSpeechRecognitionManagerDelegate::OnRecognitionStart(
    int session_id) {}

void ElectronSpeechRecognitionManagerDelegate::OnAudioStart(int session_id) {}

void ElectronSpeechRecognitionManagerDelegate::OnSoundStart(int session_id) {}

void ElectronSpeechRecognitionManagerDelegate::OnSoundEnd(int session_id) {}

void ElectronSpeechRecognitionManagerDelegate::OnAudioEnd(int session_id) {}

void ElectronSpeechRecognitionManagerDelegate::OnRecognitionEnd(
    int session_id) {}

void ElectronSpeechRecognitionManagerDelegate::OnRecognitionResults(
    int session_id,
    const std::vector<media::mojom::WebSpeechRecognitionResultPtr>& results) {}

void ElectronSpeechRecognitionManagerDelegate::OnRecognitionError(
    int session_id,
    const media::mojom::SpeechRecognitionError& error) {}

void ElectronSpeechRecognitionManagerDelegate::OnAudioLevelsChange(
    int session_id,
    float volume,
    float noise_volume) {}

void ElectronSpeechRecognitionManagerDelegate::CheckRecognitionIsAllowed(
    int session_id,
    base::OnceCallback<void(bool ask_user, bool is_allowed)> callback) {
  std::move(callback).Run(true, true);
}

content::SpeechRecognitionEventListener*
ElectronSpeechRecognitionManagerDelegate::GetEventListener() {
  return this;
}

bool ElectronSpeechRecognitionManagerDelegate::FilterProfanities(
    int render_process_id) {
  return false;
}

void ElectronSpeechRecognitionManagerDelegate::BindSpeechRecognitionContext(
    mojo::PendingReceiver<media::mojom::SpeechRecognitionContext> receiver) {}

}  // namespace electron
