// Copyright (c) 2015 Felix Rieseberg <feriese@microsoft.com> and Jason Poon
// <jason.poon@microsoft.com>. All rights reserved.
// Copyright (c) 2015 Ryan McShane <rmcshane@bandwidth.com> and Brandon Smith
// <bsmith@bandwidth.com>
// Thanks to both of those folks mentioned above who first thought up a bunch of
// this code
// and released it as MIT to the world.

#include "shell/browser/notifications/win/windows_toast_notification.h"

#include <string_view>

#include <shlobj.h>
#include <wrl\wrappers\corewrappers.h>

#include "base/environment.h"
#include "base/hash/hash.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util_win.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "shell/browser/notifications/notification_delegate.h"
#include "shell/browser/notifications/win/notification_presenter_win.h"
#include "shell/browser/win/scoped_hstring.h"
#include "shell/common/application_info.h"
#include "ui/base/l10n/l10n_util_win.h"
#include "ui/strings/grit/ui_strings.h"

using ABI::Windows::Data::Xml::Dom::IXmlAttribute;
using ABI::Windows::Data::Xml::Dom::IXmlDocument;
using ABI::Windows::Data::Xml::Dom::IXmlDocumentIO;
using ABI::Windows::Data::Xml::Dom::IXmlElement;
using ABI::Windows::Data::Xml::Dom::IXmlNamedNodeMap;
using ABI::Windows::Data::Xml::Dom::IXmlNode;
using ABI::Windows::Data::Xml::Dom::IXmlNodeList;
using ABI::Windows::Data::Xml::Dom::IXmlText;
using Microsoft::WRL::Wrappers::HStringReference;

namespace winui = ABI::Windows::UI;

#define RETURN_IF_FAILED(hr)                           \
  do {                                                 \
    if (const HRESULT _hrTemp = hr; FAILED(_hrTemp)) { \
      return _hrTemp;                                  \
    }                                                  \
  } while (false)

#define REPORT_AND_RETURN_IF_FAILED(hr, msg)                              \
  do {                                                                    \
    if (const HRESULT _hrTemp = hr; FAILED(_hrTemp)) {                    \
      std::string _err =                                                  \
          base::StrCat({msg, ", ERROR ", base::NumberToString(_hrTemp)}); \
      DebugLog(_err);                                                     \
      Notification::NotificationFailed(_err);                             \
      return _hrTemp;                                                     \
    }                                                                     \
  } while (false)

namespace electron {

namespace {

// This string needs to be max 16 characters to work on Windows 10 prior to
// applying Creators Update (build 15063).
constexpr wchar_t kGroup[] = L"Notifications";

void DebugLog(std::string_view log_msg) {
  if (base::Environment::Create()->HasVar("ELECTRON_DEBUG_NOTIFICATIONS"))
    LOG(INFO) << log_msg;
}

std::wstring GetTag(const std::string& notification_id) {
  return base::NumberToWString(base::Hash(notification_id));
}

}  // namespace

// static
ComPtr<winui::Notifications::IToastNotificationManagerStatics>
    WindowsToastNotification::toast_manager_;

// static
ComPtr<winui::Notifications::IToastNotifier>
    WindowsToastNotification::toast_notifier_;

// static
bool WindowsToastNotification::Initialize() {
  // Just initialize, don't care if it fails or already initialized.
  Windows::Foundation::Initialize(RO_INIT_MULTITHREADED);

  ScopedHString toast_manager_str(
      RuntimeClass_Windows_UI_Notifications_ToastNotificationManager);
  if (!toast_manager_str.success())
    return false;
  if (FAILED(Windows::Foundation::GetActivationFactory(toast_manager_str,
                                                       &toast_manager_)))
    return false;

  if (IsRunningInDesktopBridge()) {
    // Ironically, the Desktop Bridge / UWP environment
    // requires us to not give Windows an appUserModelId.
    return SUCCEEDED(toast_manager_->CreateToastNotifier(&toast_notifier_));
  } else {
    ScopedHString app_id;
    if (!GetAppUserModelID(&app_id))
      return false;

    return SUCCEEDED(
        toast_manager_->CreateToastNotifierWithId(app_id, &toast_notifier_));
  }
}

WindowsToastNotification::WindowsToastNotification(
    NotificationDelegate* delegate,
    NotificationPresenter* presenter)
    : Notification(delegate, presenter) {}

WindowsToastNotification::~WindowsToastNotification() {
  // Remove the notification on exit.
  if (toast_notification_) {
    RemoveCallbacks(toast_notification_.Get());
  }
}

void WindowsToastNotification::Show(const NotificationOptions& options) {
  if (SUCCEEDED(ShowInternal(options))) {
    DebugLog("Notification created");

    if (delegate())
      delegate()->NotificationDisplayed();
  }
}

void WindowsToastNotification::Remove() {
  DebugLog("Removing notification from action center");

  ComPtr<winui::Notifications::IToastNotificationManagerStatics2>
      toast_manager2;
  if (FAILED(toast_manager_.As(&toast_manager2)))
    return;

  ComPtr<winui::Notifications::IToastNotificationHistory> notification_history;
  if (FAILED(toast_manager2->get_History(&notification_history)))
    return;

  ScopedHString app_id;
  if (!GetAppUserModelID(&app_id))
    return;

  ScopedHString group(kGroup);
  ScopedHString tag(GetTag(notification_id()));
  notification_history->RemoveGroupedTagWithId(tag, group, app_id);
}

void WindowsToastNotification::Dismiss() {
  DebugLog("Hiding notification");

  toast_notifier_->Hide(toast_notification_.Get());
}

HRESULT WindowsToastNotification::ShowInternal(
    const NotificationOptions& options) {
  ComPtr<IXmlDocument> toast_xml;
  // The custom xml takes priority over the preset template.
  if (!options.toast_xml.empty()) {
    REPORT_AND_RETURN_IF_FAILED(
        XmlDocumentFromString(base::as_wcstr(options.toast_xml), &toast_xml),
        "XML: Invalid XML");
  } else {
    auto* presenter_win = static_cast<NotificationPresenterWin*>(presenter());
    std::wstring icon_path =
        presenter_win->SaveIconToFilesystem(options.icon, options.icon_url);
    REPORT_AND_RETURN_IF_FAILED(
        GetToastXml(toast_manager_.Get(), options.title, options.msg, icon_path,
                    options.timeout_type, options.silent, &toast_xml),
        "XML: Failed to create XML document");
  }

  ScopedHString toast_str(
      RuntimeClass_Windows_UI_Notifications_ToastNotification);
  if (!toast_str.success()) {
    NotificationFailed("Creating ScopedHString failed");
    return E_FAIL;
  }

  ComPtr<winui::Notifications::IToastNotificationFactory> toast_factory;
  REPORT_AND_RETURN_IF_FAILED(
      Windows::Foundation::GetActivationFactory(toast_str, &toast_factory),
      "WinAPI: GetActivationFactory failed");

  REPORT_AND_RETURN_IF_FAILED(toast_factory->CreateToastNotification(
                                  toast_xml.Get(), &toast_notification_),
                              "WinAPI: CreateToastNotification failed");

  ComPtr<winui::Notifications::IToastNotification2> toast2;
  REPORT_AND_RETURN_IF_FAILED(
      toast_notification_->QueryInterface(IID_PPV_ARGS(&toast2)),
      "WinAPI: Getting Notification interface failed");

  ScopedHString group(kGroup);
  REPORT_AND_RETURN_IF_FAILED(toast2->put_Group(group),
                              "WinAPI: Setting group failed");

  ScopedHString tag(GetTag(notification_id()));
  REPORT_AND_RETURN_IF_FAILED(toast2->put_Tag(tag),
                              "WinAPI: Setting tag failed");

  REPORT_AND_RETURN_IF_FAILED(SetupCallbacks(toast_notification_.Get()),
                              "WinAPI: SetupCallbacks failed");

  REPORT_AND_RETURN_IF_FAILED(toast_notifier_->Show(toast_notification_.Get()),
                              "WinAPI: Show failed");
  return S_OK;
}

HRESULT WindowsToastNotification::GetToastXml(
    winui::Notifications::IToastNotificationManagerStatics* toastManager,
    const std::u16string& title,
    const std::u16string& msg,
    const std::wstring& icon_path,
    const std::u16string& timeout_type,
    bool silent,
    IXmlDocument** toast_xml) {
  winui::Notifications::ToastTemplateType template_type;
  if (title.empty() || msg.empty()) {
    // Single line toast.
    template_type =
        icon_path.empty()
            ? winui::Notifications::ToastTemplateType_ToastText01
            : winui::Notifications::ToastTemplateType_ToastImageAndText01;
    REPORT_AND_RETURN_IF_FAILED(
        toast_manager_->GetTemplateContent(template_type, toast_xml),
        "XML: Fetching XML ToastImageAndText01 template failed");
    std::u16string toastMsg = title.empty() ? msg : title;
    // we can't create an empty notification
    toastMsg = toastMsg.empty() ? u"[no message]" : toastMsg;
    REPORT_AND_RETURN_IF_FAILED(
        SetXmlText(*toast_xml, toastMsg),
        "XML: Filling XML ToastImageAndText01 template failed");
  } else {
    // Title and body toast.
    template_type =
        icon_path.empty()
            ? winui::Notifications::ToastTemplateType_ToastText02
            : winui::Notifications::ToastTemplateType_ToastImageAndText02;
    REPORT_AND_RETURN_IF_FAILED(
        toastManager->GetTemplateContent(template_type, toast_xml),
        "XML: Fetching XML ToastImageAndText02 template failed");
    REPORT_AND_RETURN_IF_FAILED(
        SetXmlText(*toast_xml, title, msg),
        "XML: Filling XML ToastImageAndText02 template failed");
  }

  // Configure the toast's timeout settings
  if (timeout_type == u"never") {
    REPORT_AND_RETURN_IF_FAILED(
        (SetXmlScenarioReminder(*toast_xml)),
        "XML: Setting \"scenario\" option on notification failed");
  }

  // Configure the toast's notification sound
  if (silent) {
    REPORT_AND_RETURN_IF_FAILED(
        SetXmlAudioSilent(*toast_xml),
        "XML: Setting \"silent\" option on notification failed");
  }

  // Configure the toast's image
  if (!icon_path.empty()) {
    REPORT_AND_RETURN_IF_FAILED(
        SetXmlImage(*toast_xml, icon_path),
        "XML: Setting \"icon\" option on notification failed");
  }

  return S_OK;
}

HRESULT WindowsToastNotification::SetXmlScenarioReminder(IXmlDocument* doc) {
  ScopedHString tag(L"toast");
  if (!tag.success())
    return false;

  ComPtr<IXmlNodeList> node_list;
  RETURN_IF_FAILED(doc->GetElementsByTagName(tag, &node_list));

  // Check that root "toast" node exists
  ComPtr<IXmlNode> root;
  RETURN_IF_FAILED(node_list->Item(0, &root));

  // get attributes of root "toast" node
  ComPtr<IXmlNamedNodeMap> toast_attributes;
  RETURN_IF_FAILED(root->get_Attributes(&toast_attributes));

  ComPtr<IXmlAttribute> scenario_attribute;
  ScopedHString scenario_str(L"scenario");
  RETURN_IF_FAILED(doc->CreateAttribute(scenario_str, &scenario_attribute));

  ComPtr<IXmlNode> scenario_attribute_node;
  RETURN_IF_FAILED(scenario_attribute.As(&scenario_attribute_node));

  ScopedHString scenario_value(L"reminder");
  if (!scenario_value.success())
    return E_FAIL;

  ComPtr<IXmlText> scenario_text;
  RETURN_IF_FAILED(doc->CreateTextNode(scenario_value, &scenario_text));

  ComPtr<IXmlNode> scenario_node;
  RETURN_IF_FAILED(scenario_text.As(&scenario_node));

  ComPtr<IXmlNode> scenario_backup_node;
  RETURN_IF_FAILED(scenario_attribute_node->AppendChild(scenario_node.Get(),
                                                        &scenario_backup_node));

  ComPtr<IXmlNode> scenario_attribute_pnode;
  RETURN_IF_FAILED(toast_attributes.Get()->SetNamedItem(
      scenario_attribute_node.Get(), &scenario_attribute_pnode));

  // Create "actions" wrapper
  ComPtr<IXmlElement> actions_wrapper_element;
  ScopedHString actions_wrapper_str(L"actions");
  RETURN_IF_FAILED(
      doc->CreateElement(actions_wrapper_str, &actions_wrapper_element));

  ComPtr<IXmlNode> actions_wrapper_node_tmp;
  RETURN_IF_FAILED(actions_wrapper_element.As(&actions_wrapper_node_tmp));

  // Append actions wrapper node to toast xml
  ComPtr<IXmlNode> actions_wrapper_node;
  RETURN_IF_FAILED(
      root->AppendChild(actions_wrapper_node_tmp.Get(), &actions_wrapper_node));

  ComPtr<IXmlNamedNodeMap> attributes_actions_wrapper;
  RETURN_IF_FAILED(
      actions_wrapper_node->get_Attributes(&attributes_actions_wrapper));

  // Add a "Dismiss" button
  // Create "action" tag
  ComPtr<IXmlElement> action_element;
  ScopedHString action_str(L"action");
  RETURN_IF_FAILED(doc->CreateElement(action_str, &action_element));

  ComPtr<IXmlNode> action_node_tmp;
  RETURN_IF_FAILED(action_element.As(&action_node_tmp));

  // Append action node to actions wrapper in toast xml
  ComPtr<IXmlNode> action_node;
  RETURN_IF_FAILED(
      actions_wrapper_node->AppendChild(action_node_tmp.Get(), &action_node));

  // Setup attributes for action
  ComPtr<IXmlNamedNodeMap> action_attributes;
  RETURN_IF_FAILED(action_node->get_Attributes(&action_attributes));

  // Create activationType attribute
  ComPtr<IXmlAttribute> activation_type_attribute;
  ScopedHString activation_type_str(L"activationType");
  RETURN_IF_FAILED(
      doc->CreateAttribute(activation_type_str, &activation_type_attribute));

  ComPtr<IXmlNode> activation_type_attribute_node;
  RETURN_IF_FAILED(
      activation_type_attribute.As(&activation_type_attribute_node));

  // Set activationType attribute to system
  ScopedHString activation_type_value(L"system");
  if (!activation_type_value.success())
    return E_FAIL;

  ComPtr<IXmlText> activation_type_text;
  RETURN_IF_FAILED(
      doc->CreateTextNode(activation_type_value, &activation_type_text));

  ComPtr<IXmlNode> activation_type_node;
  RETURN_IF_FAILED(activation_type_text.As(&activation_type_node));

  ComPtr<IXmlNode> activation_type_backup_node;
  RETURN_IF_FAILED(activation_type_attribute_node->AppendChild(
      activation_type_node.Get(), &activation_type_backup_node));

  // Add activation type to the action attributes
  ComPtr<IXmlNode> activation_type_attribute_pnode;
  RETURN_IF_FAILED(action_attributes.Get()->SetNamedItem(
      activation_type_attribute_node.Get(), &activation_type_attribute_pnode));

  // Create arguments attribute
  ComPtr<IXmlAttribute> arguments_attribute;
  ScopedHString arguments_str(L"arguments");
  RETURN_IF_FAILED(doc->CreateAttribute(arguments_str, &arguments_attribute));

  ComPtr<IXmlNode> arguments_attribute_node;
  RETURN_IF_FAILED(arguments_attribute.As(&arguments_attribute_node));

  // Set arguments attribute to dismiss
  ScopedHString arguments_value(L"dismiss");
  if (!arguments_value.success())
    return E_FAIL;

  ComPtr<IXmlText> arguments_text;
  RETURN_IF_FAILED(doc->CreateTextNode(arguments_value, &arguments_text));

  ComPtr<IXmlNode> arguments_node;
  RETURN_IF_FAILED(arguments_text.As(&arguments_node));

  ComPtr<IXmlNode> arguments_backup_node;
  RETURN_IF_FAILED(arguments_attribute_node->AppendChild(
      arguments_node.Get(), &arguments_backup_node));

  // Add arguments to the action attributes
  ComPtr<IXmlNode> arguments_attribute_pnode;
  RETURN_IF_FAILED(action_attributes.Get()->SetNamedItem(
      arguments_attribute_node.Get(), &arguments_attribute_pnode));

  // Create content attribute
  ComPtr<IXmlAttribute> content_attribute;
  ScopedHString content_str(L"content");
  RETURN_IF_FAILED(doc->CreateAttribute(content_str, &content_attribute));

  ComPtr<IXmlNode> content_attribute_node;
  RETURN_IF_FAILED(content_attribute.As(&content_attribute_node));

  // Set content attribute to Dismiss
  ScopedHString content_value(l10n_util::GetWideString(IDS_APP_CLOSE));
  if (!content_value.success())
    return E_FAIL;

  ComPtr<IXmlText> content_text;
  RETURN_IF_FAILED(doc->CreateTextNode(content_value, &content_text));

  ComPtr<IXmlNode> content_node;
  RETURN_IF_FAILED(content_text.As(&content_node));

  ComPtr<IXmlNode> content_backup_node;
  RETURN_IF_FAILED(content_attribute_node->AppendChild(content_node.Get(),
                                                       &content_backup_node));

  // Add content to the action attributes
  ComPtr<IXmlNode> content_attribute_pnode;
  return action_attributes.Get()->SetNamedItem(content_attribute_node.Get(),
                                               &content_attribute_pnode);
}

HRESULT WindowsToastNotification::SetXmlAudioSilent(IXmlDocument* doc) {
  ScopedHString tag(L"toast");
  if (!tag.success())
    return E_FAIL;

  ComPtr<IXmlNodeList> node_list;
  RETURN_IF_FAILED(doc->GetElementsByTagName(tag, &node_list));

  ComPtr<IXmlNode> root;
  RETURN_IF_FAILED(node_list->Item(0, &root));

  ComPtr<IXmlElement> audio_element;
  ScopedHString audio_str(L"audio");
  RETURN_IF_FAILED(doc->CreateElement(audio_str, &audio_element));

  ComPtr<IXmlNode> audio_node_tmp;
  RETURN_IF_FAILED(audio_element.As(&audio_node_tmp));

  // Append audio node to toast xml
  ComPtr<IXmlNode> audio_node;
  RETURN_IF_FAILED(root->AppendChild(audio_node_tmp.Get(), &audio_node));

  // Create silent attribute
  ComPtr<IXmlNamedNodeMap> attributes;
  RETURN_IF_FAILED(audio_node->get_Attributes(&attributes));

  ComPtr<IXmlAttribute> silent_attribute;
  ScopedHString silent_str(L"silent");
  RETURN_IF_FAILED(doc->CreateAttribute(silent_str, &silent_attribute));

  ComPtr<IXmlNode> silent_attribute_node;
  RETURN_IF_FAILED(silent_attribute.As(&silent_attribute_node));

  // Set silent attribute to true
  ScopedHString silent_value(L"true");
  if (!silent_value.success())
    return E_FAIL;

  ComPtr<IXmlText> silent_text;
  RETURN_IF_FAILED(doc->CreateTextNode(silent_value, &silent_text));

  ComPtr<IXmlNode> silent_node;
  RETURN_IF_FAILED(silent_text.As(&silent_node));

  ComPtr<IXmlNode> child_node;
  RETURN_IF_FAILED(
      silent_attribute_node->AppendChild(silent_node.Get(), &child_node));

  ComPtr<IXmlNode> silent_attribute_pnode;
  return attributes.Get()->SetNamedItem(silent_attribute_node.Get(),
                                        &silent_attribute_pnode);
}

HRESULT WindowsToastNotification::SetXmlText(IXmlDocument* doc,
                                             const std::u16string& text) {
  ScopedHString tag;
  ComPtr<IXmlNodeList> node_list;
  RETURN_IF_FAILED(GetTextNodeList(&tag, doc, &node_list, 1));

  ComPtr<IXmlNode> node;
  RETURN_IF_FAILED(node_list->Item(0, &node));

  return AppendTextToXml(doc, node.Get(), text);
}

HRESULT WindowsToastNotification::SetXmlText(IXmlDocument* doc,
                                             const std::u16string& title,
                                             const std::u16string& body) {
  ScopedHString tag;
  ComPtr<IXmlNodeList> node_list;
  RETURN_IF_FAILED(GetTextNodeList(&tag, doc, &node_list, 2));

  ComPtr<IXmlNode> node;
  RETURN_IF_FAILED(node_list->Item(0, &node));
  RETURN_IF_FAILED(AppendTextToXml(doc, node.Get(), title));
  RETURN_IF_FAILED(node_list->Item(1, &node));

  return AppendTextToXml(doc, node.Get(), body);
}

HRESULT WindowsToastNotification::SetXmlImage(IXmlDocument* doc,
                                              const std::wstring& icon_path) {
  ScopedHString tag(L"image");
  if (!tag.success())
    return E_FAIL;

  ComPtr<IXmlNodeList> node_list;
  RETURN_IF_FAILED(doc->GetElementsByTagName(tag, &node_list));

  ComPtr<IXmlNode> image_node;
  RETURN_IF_FAILED(node_list->Item(0, &image_node));

  ComPtr<IXmlNamedNodeMap> attrs;
  RETURN_IF_FAILED(image_node->get_Attributes(&attrs));

  ScopedHString src(L"src");
  if (!src.success())
    return E_FAIL;

  ComPtr<IXmlNode> src_attr;
  RETURN_IF_FAILED(attrs->GetNamedItem(src, &src_attr));

  const ScopedHString img_path{icon_path};
  if (!img_path.success())
    return E_FAIL;

  ComPtr<IXmlText> src_text;
  RETURN_IF_FAILED(doc->CreateTextNode(img_path, &src_text));

  ComPtr<IXmlNode> src_node;
  RETURN_IF_FAILED(src_text.As(&src_node));

  ComPtr<IXmlNode> child_node;
  return src_attr->AppendChild(src_node.Get(), &child_node);
}

HRESULT WindowsToastNotification::GetTextNodeList(ScopedHString* tag,
                                                  IXmlDocument* doc,
                                                  IXmlNodeList** node_list,
                                                  uint32_t req_length) {
  tag->Reset(L"text");
  if (!tag->success())
    return E_FAIL;

  RETURN_IF_FAILED(doc->GetElementsByTagName(*tag, node_list));

  uint32_t node_length;
  RETURN_IF_FAILED((*node_list)->get_Length(&node_length));

  return node_length >= req_length;
}

HRESULT WindowsToastNotification::AppendTextToXml(IXmlDocument* doc,
                                                  IXmlNode* node,
                                                  const std::u16string& text) {
  ScopedHString str(base::as_wcstr(text));
  if (!str.success())
    return E_FAIL;

  ComPtr<IXmlText> xml_text;
  RETURN_IF_FAILED(doc->CreateTextNode(str, &xml_text));

  ComPtr<IXmlNode> text_node;
  RETURN_IF_FAILED(xml_text.As(&text_node));

  ComPtr<IXmlNode> append_node;
  RETURN_IF_FAILED(node->AppendChild(text_node.Get(), &append_node));

  return S_OK;
}

HRESULT WindowsToastNotification::XmlDocumentFromString(
    const wchar_t* xmlString,
    IXmlDocument** doc) {
  ComPtr<IXmlDocument> xmlDoc;
  RETURN_IF_FAILED(Windows::Foundation::ActivateInstance(
      HStringReference(RuntimeClass_Windows_Data_Xml_Dom_XmlDocument).Get(),
      &xmlDoc));

  ComPtr<IXmlDocumentIO> docIO;
  RETURN_IF_FAILED(xmlDoc.As(&docIO));

  RETURN_IF_FAILED(docIO->LoadXml(HStringReference(xmlString).Get()));

  return xmlDoc.CopyTo(doc);
}

HRESULT WindowsToastNotification::SetupCallbacks(
    winui::Notifications::IToastNotification* toast) {
  event_handler_ = Make<ToastEventHandler>(this);
  RETURN_IF_FAILED(
      toast->add_Activated(event_handler_.Get(), &activated_token_));
  RETURN_IF_FAILED(
      toast->add_Dismissed(event_handler_.Get(), &dismissed_token_));
  RETURN_IF_FAILED(toast->add_Failed(event_handler_.Get(), &failed_token_));
  return S_OK;
}

bool WindowsToastNotification::RemoveCallbacks(
    winui::Notifications::IToastNotification* toast) {
  if (FAILED(toast->remove_Activated(activated_token_)))
    return false;

  if (FAILED(toast->remove_Dismissed(dismissed_token_)))
    return false;

  return SUCCEEDED(toast->remove_Failed(failed_token_));
}

/*
/ Toast Event Handler
*/
ToastEventHandler::ToastEventHandler(Notification* notification)
    : notification_(notification->GetWeakPtr()) {}

ToastEventHandler::~ToastEventHandler() = default;

IFACEMETHODIMP ToastEventHandler::Invoke(
    winui::Notifications::IToastNotification* sender,
    IInspectable* args) {
  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE,
      base::BindOnce(&Notification::NotificationClicked, notification_));
  DebugLog("Notification clicked");

  return S_OK;
}

IFACEMETHODIMP ToastEventHandler::Invoke(
    winui::Notifications::IToastNotification* sender,
    winui::Notifications::IToastDismissedEventArgs* e) {
  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE, base::BindOnce(&Notification::NotificationDismissed,
                                notification_, false));
  DebugLog("Notification dismissed");
  return S_OK;
}

IFACEMETHODIMP ToastEventHandler::Invoke(
    winui::Notifications::IToastNotification* sender,
    winui::Notifications::IToastFailedEventArgs* e) {
  HRESULT error;
  e->get_ErrorCode(&error);
  std::string errorMessage =
      "Notification failed. HRESULT:" + std::to_string(error);
  content::GetUIThreadTaskRunner({})->PostTask(
      FROM_HERE, base::BindOnce(&Notification::NotificationFailed,
                                notification_, errorMessage));
  DebugLog(errorMessage);

  return S_OK;
}

}  // namespace electron
