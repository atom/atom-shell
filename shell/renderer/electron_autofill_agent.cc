// Copyright (c) 2017 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/renderer/electron_autofill_agent.h"

#include <string>
#include <utility>
#include <vector>

#include "content/public/renderer/render_frame.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "third_party/blink/public/common/input/web_keyboard_event.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_option_element.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/geometry/rect_f.h"

namespace electron {

namespace {
const size_t kMaxStringLength = 1024;
const size_t kMaxListSize = 512;

// Copied from components/autofill/content/renderer/form_autofill_util.cc
void TrimStringVectorForIPC(std::vector<std::u16string>* strings) {
  // Limit the size of the vector.
  if (strings->size() > kMaxListSize)
    strings->resize(kMaxListSize);

  // Limit the size of the strings in the vector.
  for (auto& string : *strings) {
    if (string.length() > kMaxStringLength)
      string.resize(kMaxStringLength);
  }
}

// Copied from components/autofill/content/renderer/form_autofill_util.cc.
void GetDataListSuggestions(const blink::WebInputElement& element,
                            std::vector<std::u16string>* values,
                            std::vector<std::u16string>* labels) {
  for (const auto& option : element.FilteredDataListOptions()) {
    values->push_back(option.Value().Utf16());
    if (option.Value() != option.Label())
      labels->push_back(option.Label().Utf16());
    else
      labels->emplace_back();
  }

  TrimStringVectorForIPC(values);
  TrimStringVectorForIPC(labels);
}

}  // namespace

AutofillAgent::AutofillAgent(content::RenderFrame* frame,
                             blink::AssociatedInterfaceRegistry* registry)
    : content::RenderFrameObserver(frame) {
  render_frame()->GetWebFrame()->SetAutofillClient(this);
  registry->AddInterface<mojom::ElectronAutofillAgent>(base::BindRepeating(
      &AutofillAgent::BindPendingReceiver, base::Unretained(this)));
}

AutofillAgent::~AutofillAgent() = default;

void AutofillAgent::BindPendingReceiver(
    mojo::PendingAssociatedReceiver<mojom::ElectronAutofillAgent>
        pending_receiver) {
  receiver_.Bind(std::move(pending_receiver));
}

void AutofillAgent::OnDestruct() {
  Shutdown();
  base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE,
                                                                this);
}

void AutofillAgent::Shutdown() {
  receiver_.reset();
  weak_ptr_factory_.InvalidateWeakPtrs();
}

void AutofillAgent::DidChangeScrollOffset() {
  HidePopup();
}

void AutofillAgent::FocusedElementChanged(const blink::WebElement&) {
  focused_node_was_last_clicked_ = false;
  was_focused_before_now_ = false;
  HidePopup();
}

void AutofillAgent::TextFieldDidEndEditing(const blink::WebInputElement&) {
  HidePopup();
}

void AutofillAgent::TextFieldDidChange(
    const blink::WebFormControlElement& element) {
  if (!IsUserGesture() && !render_frame()->IsPasting())
    return;

  weak_ptr_factory_.InvalidateWeakPtrs();
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&AutofillAgent::TextFieldDidChangeImpl,
                                weak_ptr_factory_.GetWeakPtr(), element));
}

void AutofillAgent::TextFieldDidChangeImpl(
    const blink::WebFormControlElement& element) {
  ShowSuggestions(element, {.requires_caret_at_end = true});
}

void AutofillAgent::TextFieldDidReceiveKeyDown(
    const blink::WebInputElement& element,
    const blink::WebKeyboardEvent& event) {
  if (event.windows_key_code == ui::VKEY_DOWN ||
      event.windows_key_code == ui::VKEY_UP) {
    ShowSuggestions(element, {.autofill_on_empty_values = true,
                              .requires_caret_at_end = true});
  }
}

void AutofillAgent::OpenTextDataListChooser(
    const blink::WebInputElement& element) {
  ShowSuggestions(element, {.autofill_on_empty_values = true});
}

void AutofillAgent::DataListOptionsChanged(
    const blink::WebInputElement& element) {
  if (!element.Focused())
    return;

  ShowSuggestions(element, {.requires_caret_at_end = true});
}

void AutofillAgent::ShowSuggestions(const blink::WebFormControlElement& element,
                                    const ShowSuggestionsOptions& options) {
  if (!element.IsEnabled() || element.IsReadOnly())
    return;
  if (!element.SuggestedValue().IsEmpty())
    return;

  const blink::WebInputElement input_element =
      element.DynamicTo<blink::WebInputElement>();
  if (!input_element.IsNull()) {
    if (!input_element.IsTextField())
      return;
    if (!input_element.SuggestedValue().IsEmpty())
      return;
  }

  // Don't attempt to autofill with values that are too large or if filling
  // criteria are not met. Keyboard Accessory may still be shown when the
  // |value| is empty, do not attempt to hide it.
  blink::WebString value = element.EditingValue();
  if (value.length() > kMaxStringLength ||
      (!options.autofill_on_empty_values && value.IsEmpty()) ||
      (options.requires_caret_at_end &&
       (element.SelectionStart() != element.SelectionEnd() ||
        element.SelectionEnd() != value.length()))) {
    // Any popup currently showing is obsolete.
    HidePopup();
    return;
  }

  std::vector<std::u16string> data_list_values;
  std::vector<std::u16string> data_list_labels;
  if (!input_element.IsNull()) {
    GetDataListSuggestions(input_element, &data_list_values, &data_list_labels);
  }

  ShowPopup(element, data_list_values, data_list_labels);
}

void AutofillAgent::DidReceiveLeftMouseDownOrGestureTapInNode(
    const blink::WebNode& node) {
  DCHECK(!node.IsNull());
  focused_node_was_last_clicked_ = node.Focused();
}

void AutofillAgent::DidCompleteFocusChangeInFrame() {
  HandleFocusChangeComplete();
}

bool AutofillAgent::IsUserGesture() const {
  return render_frame()->GetWebFrame()->HasTransientUserActivation();
}

void AutofillAgent::HidePopup() {
  GetAutofillDriver()->HideAutofillPopup();
}

void AutofillAgent::ShowPopup(const blink::WebFormControlElement& element,
                              const std::vector<std::u16string>& values,
                              const std::vector<std::u16string>& labels) {
  auto bounds = gfx::RectF{
      render_frame()->ConvertViewportToWindow(element.BoundsInWidget())};
  GetAutofillDriver()->ShowAutofillPopup(bounds, values, labels);
}

void AutofillAgent::AcceptDataListSuggestion(const std::u16string& suggestion) {
  auto element = render_frame()->GetWebFrame()->GetDocument().FocusedElement();
  if (element.IsFormControlElement()) {
    blink::WebInputElement input_element =
        element.DynamicTo<blink::WebInputElement>();
    if (!input_element.IsNull())
      input_element.SetAutofillValue(blink::WebString::FromUTF16(suggestion));
  }
}

void AutofillAgent::HandleFocusChangeComplete() {
  auto element = render_frame()->GetWebFrame()->GetDocument().FocusedElement();
  if (element.IsNull() || !element.IsFormControlElement())
    return;

  if (focused_node_was_last_clicked_ && was_focused_before_now_) {
    blink::WebInputElement input_element =
        element.DynamicTo<blink::WebInputElement>();
    if (!input_element.IsNull())
      ShowSuggestions(input_element, {.autofill_on_empty_values = true});
  }

  was_focused_before_now_ = true;
  focused_node_was_last_clicked_ = false;
}

const mojo::AssociatedRemote<mojom::ElectronAutofillDriver>&
AutofillAgent::GetAutofillDriver() {
  if (!autofill_driver_) {
    render_frame()->GetRemoteAssociatedInterfaces()->GetInterface(
        &autofill_driver_);
  }

  return autofill_driver_;
}

}  // namespace electron
