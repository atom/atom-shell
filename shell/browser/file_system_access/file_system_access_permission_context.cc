// Copyright (c) 2024 Microsoft, GmbH
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/file_system_access/file_system_access_permission_context.h"

#include <string>
#include <utility>

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/json/values_util.h"
#include "base/path_service.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/file_system_access/chrome_file_system_access_permission_context.h"  // nogncheck
#include "chrome/browser/file_system_access/file_system_access_features.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/disallow_activation_reason.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "shell/browser/electron_permission_manager.h"
#include "shell/browser/web_contents_permission_helper.h"
#include "shell/common/gin_converters/file_path_converter.h"
#include "third_party/blink/public/mojom/file_system_access/file_system_access_manager.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "url/origin.h"

namespace {

using BlockType = ChromeFileSystemAccessPermissionContext::BlockType;
using HandleType = content::FileSystemAccessPermissionContext::HandleType;
using GrantType = electron::FileSystemAccessPermissionContext::GrantType;
using blink::mojom::PermissionStatus;

#if BUILDFLAG(IS_WIN)
[[nodiscard]] constexpr bool ContainsInvalidDNSCharacter(
    base::FilePath::StringType hostname) {
  return !base::ranges::all_of(hostname, [](base::FilePath::CharType c) {
    return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') ||
           (c >= L'0' && c <= L'9') || (c == L'.') || (c == L'-');
  });
}

bool MaybeIsLocalUNCPath(const base::FilePath& path) {
  if (!path.IsNetwork()) {
    return false;
  }

  const std::vector<base::FilePath::StringType> components =
      path.GetComponents();

  // Check for server name that could represent a local system. We only
  // check for a very short list, as it is impossible to cover all different
  // variants on Windows.
  if (components.size() >= 2 &&
      (base::FilePath::CompareEqualIgnoreCase(components[1],
                                              FILE_PATH_LITERAL("localhost")) ||
       components[1] == FILE_PATH_LITERAL("127.0.0.1") ||
       components[1] == FILE_PATH_LITERAL(".") ||
       components[1] == FILE_PATH_LITERAL("?") ||
       ContainsInvalidDNSCharacter(components[1]))) {
    return true;
  }

  // In case we missed the server name check above, we also check for shares
  // ending with '$' as they represent pre-defined shares, including the local
  // drives.
  for (size_t i = 2; i < components.size(); ++i) {
    if (components[i].back() == L'$') {
      return true;
    }
  }

  return false;
}
#endif

// Describes a rule for blocking a directory, which can be constructed
// dynamically (based on state) or statically (from kBlockedPaths).
struct BlockPathRule {
  base::FilePath path;
  BlockType type;
};

bool ShouldBlockAccessToPath(const base::FilePath& path,
                             HandleType handle_type,
                             std::vector<BlockPathRule> rules) {
  DCHECK(!path.empty());
  DCHECK(path.IsAbsolute());

#if BUILDFLAG(IS_WIN)
  // On Windows, local UNC paths are rejected, as UNC path can be written in a
  // way that can bypass the blocklist.
  if (base::FeatureList::IsEnabled(
          features::kFileSystemAccessLocalUNCPathBlock) &&
      MaybeIsLocalUNCPath(path)) {
    return true;
  }
#endif

  // Add the hard-coded rules to the dynamic rules.
  for (auto const& [key, rule_path, type] :
       ChromeFileSystemAccessPermissionContext::kBlockedPaths) {
    if (key == ChromeFileSystemAccessPermissionContext::kNoBasePathKey) {
      rules.emplace_back(base::FilePath{rule_path}, type);
    } else if (base::FilePath path; base::PathService::Get(key, &path)) {
      rules.emplace_back(rule_path ? path.Append(rule_path) : path, type);
    }
  }

  base::FilePath nearest_ancestor;
  BlockType nearest_ancestor_block_type = BlockType::kDontBlockChildren;
  for (const auto& block : rules) {
    if (path == block.path || path.IsParent(block.path)) {
      DLOG(INFO) << "Blocking access to " << path
                 << " because it is a parent of " << block.path;
      return true;
    }

    if (block.path.IsParent(path) &&
        (nearest_ancestor.empty() || nearest_ancestor.IsParent(block.path))) {
      nearest_ancestor = block.path;
      nearest_ancestor_block_type = block.type;
    }
  }

  // The path we're checking is not in a potentially blocked directory, or the
  // nearest ancestor does not block access to its children. Grant access.
  if (nearest_ancestor.empty() ||
      nearest_ancestor_block_type == BlockType::kDontBlockChildren) {
    return false;
  }

  // The path we're checking is a file, and the nearest ancestor only blocks
  // access to directories. Grant access.
  if (handle_type == HandleType::kFile &&
      nearest_ancestor_block_type == BlockType::kBlockNestedDirectories) {
    return false;
  }

  // The nearest ancestor blocks access to its children, so block access.
  DLOG(INFO) << "Blocking access to " << path << " because it is inside "
             << nearest_ancestor;
  return true;
}

}  // namespace

namespace electron {

class FileSystemAccessPermissionContext::PermissionGrantImpl
    : public content::FileSystemAccessPermissionGrant {
 public:
  PermissionGrantImpl(base::WeakPtr<FileSystemAccessPermissionContext> context,
                      const url::Origin& origin,
                      const base::FilePath& path,
                      HandleType handle_type,
                      GrantType type,
                      UserAction user_action)
      : context_{std::move(context)},
        origin_{origin},
        handle_type_{handle_type},
        type_{type},
        path_{path} {}

  // FileSystemAccessPermissionGrant:
  PermissionStatus GetStatus() override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return status_;
  }

  base::FilePath GetPath() override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return path_;
  }

  void RequestPermission(
      content::GlobalRenderFrameHostId frame_id,
      UserActivationState user_activation_state,
      base::OnceCallback<void(PermissionRequestOutcome)> callback) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

    // Check if a permission request has already been processed previously. This
    // check is done first because we don't want to reset the status of a
    // permission if it has already been granted.
    if (GetStatus() != PermissionStatus::ASK || !context_) {
      if (GetStatus() == PermissionStatus::GRANTED) {
        SetStatus(PermissionStatus::GRANTED);
      }
      std::move(callback).Run(PermissionRequestOutcome::kRequestAborted);
      return;
    }

    content::RenderFrameHost* rfh = content::RenderFrameHost::FromID(frame_id);
    if (!rfh) {
      // Requested from a no longer valid RenderFrameHost.
      std::move(callback).Run(PermissionRequestOutcome::kInvalidFrame);
      return;
    }

    // Don't request permission  for an inactive RenderFrameHost as the
    // page might not distinguish properly between user denying the permission
    // and automatic rejection.
    if (rfh->IsInactiveAndDisallowActivation(
            content::DisallowActivationReasonId::
                kFileSystemAccessPermissionRequest)) {
      std::move(callback).Run(PermissionRequestOutcome::kInvalidFrame);
      return;
    }

    // We don't allow file system access from fenced frames.
    if (rfh->IsNestedWithinFencedFrame()) {
      std::move(callback).Run(PermissionRequestOutcome::kInvalidFrame);
      return;
    }

    if (user_activation_state == UserActivationState::kRequired &&
        !rfh->HasTransientUserActivation()) {
      // No permission prompts without user activation.
      std::move(callback).Run(PermissionRequestOutcome::kNoUserActivation);
      return;
    }

    if (content::WebContents::FromRenderFrameHost(rfh) == nullptr) {
      std::move(callback).Run(PermissionRequestOutcome::kInvalidFrame);
      return;
    }

    auto origin = rfh->GetLastCommittedOrigin().GetURL();
    if (url::Origin::Create(origin) != origin_) {
      // Third party iframes are not allowed to request more permissions.
      std::move(callback).Run(PermissionRequestOutcome::kThirdPartyContext);
      return;
    }

    auto* permission_manager =
        static_cast<electron::ElectronPermissionManager*>(
            context_->browser_context()->GetPermissionControllerDelegate());
    if (!permission_manager) {
      std::move(callback).Run(PermissionRequestOutcome::kRequestAborted);
      return;
    }

    blink::PermissionType type = static_cast<blink::PermissionType>(
        electron::WebContentsPermissionHelper::PermissionType::FILE_SYSTEM);

    base::Value::Dict details;
    details.Set("filePath", base::FilePathToValue(path_));
    details.Set("isDirectory", handle_type_ == HandleType::kDirectory);
    details.Set("fileAccessType",
                type_ == GrantType::kWrite ? "writable" : "readable");

    permission_manager->RequestPermissionWithDetails(
        type, rfh, origin, false, std::move(details),
        base::BindOnce(&PermissionGrantImpl::OnPermissionRequestResult, this,
                       std::move(callback)));
  }

  const url::Origin& origin() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return origin_;
  }

  HandleType handle_type() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return handle_type_;
  }

  GrantType type() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return type_;
  }

  void SetStatus(PermissionStatus new_status) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

    auto permission_changed = status_ != new_status;
    status_ = new_status;

    if (permission_changed) {
      NotifyPermissionStatusChanged();
    }
  }

  static void UpdateGrantPath(
      std::map<base::FilePath, PermissionGrantImpl*>& grants,
      const base::FilePath& old_path,
      const base::FilePath& new_path) {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    auto entry_it = base::ranges::find_if(
        grants,
        [&old_path](const auto& entry) { return entry.first == old_path; });

    if (entry_it == grants.end()) {
      // There must be an entry for an ancestor of this entry. Nothing to do
      // here.
      return;
    }

    DCHECK_EQ(entry_it->second->GetStatus(), PermissionStatus::GRANTED);

    auto* const grant_impl = entry_it->second;
    grant_impl->SetPath(new_path);

    // Update the permission grant's key in the map of active permissions.
    grants.erase(entry_it);
    grants.emplace(new_path, grant_impl);
  }

 protected:
  ~PermissionGrantImpl() override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    if (context_) {
      context_->PermissionGrantDestroyed(this);
    }
  }

 private:
  void OnPermissionRequestResult(
      base::OnceCallback<void(PermissionRequestOutcome)> callback,
      blink::mojom::PermissionStatus status) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

    if (status == blink::mojom::PermissionStatus::GRANTED) {
      SetStatus(PermissionStatus::GRANTED);
      std::move(callback).Run(PermissionRequestOutcome::kUserGranted);
    } else {
      SetStatus(PermissionStatus::DENIED);
      std::move(callback).Run(PermissionRequestOutcome::kUserDenied);
    }
  }

  void SetPath(const base::FilePath& new_path) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

    if (path_ == new_path)
      return;

    path_ = new_path;
    NotifyPermissionStatusChanged();
  }

  SEQUENCE_CHECKER(sequence_checker_);

  base::WeakPtr<FileSystemAccessPermissionContext> const context_;
  const url::Origin origin_;
  const HandleType handle_type_;
  const GrantType type_;
  base::FilePath path_;

  // This member should only be updated via SetStatus().
  PermissionStatus status_ = PermissionStatus::ASK;
};

struct FileSystemAccessPermissionContext::OriginState {
  // Raw pointers, owned collectively by all the handles that reference this
  // grant. When last reference goes away this state is cleared as well by
  // PermissionGrantDestroyed().
  std::map<base::FilePath, PermissionGrantImpl*> read_grants;
  std::map<base::FilePath, PermissionGrantImpl*> write_grants;
};

FileSystemAccessPermissionContext::FileSystemAccessPermissionContext(
    content::BrowserContext* browser_context)
    : browser_context_(browser_context) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

FileSystemAccessPermissionContext::~FileSystemAccessPermissionContext() =
    default;

scoped_refptr<content::FileSystemAccessPermissionGrant>
FileSystemAccessPermissionContext::GetReadPermissionGrant(
    const url::Origin& origin,
    const base::FilePath& path,
    HandleType handle_type,
    UserAction user_action) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // operator[] might insert a new OriginState in |active_permissions_map_|,
  // but that is exactly what we want.
  auto& origin_state = active_permissions_map_[origin];
  auto*& existing_grant = origin_state.read_grants[path];
  scoped_refptr<PermissionGrantImpl> new_grant;

  if (existing_grant && existing_grant->handle_type() != handle_type) {
    // |path| changed from being a directory to being a file or vice versa,
    // don't just re-use the existing grant but revoke the old grant before
    // creating a new grant.
    existing_grant->SetStatus(PermissionStatus::DENIED);
    existing_grant = nullptr;
  }

  if (!existing_grant) {
    new_grant = base::MakeRefCounted<PermissionGrantImpl>(
        weak_factory_.GetWeakPtr(), origin, path, handle_type, GrantType::kRead,
        user_action);
    existing_grant = new_grant.get();
  }

  // If a parent directory is already readable this new grant should also be
  // readable.
  if (new_grant &&
      AncestorHasActivePermission(origin, path, GrantType::kRead)) {
    existing_grant->SetStatus(PermissionStatus::GRANTED);
  } else {
    switch (user_action) {
      case UserAction::kOpen:
      case UserAction::kSave:
        // Open and Save dialog only grant read access for individual files.
        if (handle_type == HandleType::kDirectory) {
          break;
        }
        [[fallthrough]];
      case UserAction::kDragAndDrop:
        // Drag&drop grants read access for all handles.
        existing_grant->SetStatus(PermissionStatus::GRANTED);
        break;
      case UserAction::kLoadFromStorage:
      case UserAction::kNone:
        break;
    }
  }

  return existing_grant;
}

scoped_refptr<content::FileSystemAccessPermissionGrant>
FileSystemAccessPermissionContext::GetWritePermissionGrant(
    const url::Origin& origin,
    const base::FilePath& path,
    HandleType handle_type,
    UserAction user_action) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // operator[] might insert a new OriginState in |active_permissions_map_|,
  // but that is exactly what we want.
  auto& origin_state = active_permissions_map_[origin];
  auto*& existing_grant = origin_state.write_grants[path];
  scoped_refptr<PermissionGrantImpl> new_grant;

  if (existing_grant && existing_grant->handle_type() != handle_type) {
    // |path| changed from being a directory to being a file or vice versa,
    // don't just re-use the existing grant but revoke the old grant before
    // creating a new grant.
    existing_grant->SetStatus(PermissionStatus::DENIED);
    existing_grant = nullptr;
  }

  if (!existing_grant) {
    new_grant = base::MakeRefCounted<PermissionGrantImpl>(
        weak_factory_.GetWeakPtr(), origin, path, handle_type,
        GrantType::kWrite, user_action);
    existing_grant = new_grant.get();
  }

  // If a parent directory is already writable this new grant should also be
  // writable.
  if (new_grant &&
      AncestorHasActivePermission(origin, path, GrantType::kWrite)) {
    existing_grant->SetStatus(PermissionStatus::GRANTED);
  } else {
    switch (user_action) {
      case UserAction::kSave:
        // Only automatically grant write access for save dialogs.
        existing_grant->SetStatus(PermissionStatus::GRANTED);
        break;
      case UserAction::kOpen:
      case UserAction::kDragAndDrop:
      case UserAction::kLoadFromStorage:
      case UserAction::kNone:
        break;
    }
  }

  return existing_grant;
}

bool FileSystemAccessPermissionContext::CanObtainReadPermission(
    const url::Origin& origin) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return true;
}

bool FileSystemAccessPermissionContext::CanObtainWritePermission(
    const url::Origin& origin) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return true;
}

void FileSystemAccessPermissionContext::ConfirmSensitiveEntryAccess(
    const url::Origin& origin,
    PathType path_type,
    const base::FilePath& path,
    HandleType handle_type,
    UserAction user_action,
    content::GlobalRenderFrameHostId frame_id,
    base::OnceCallback<void(SensitiveEntryResult)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto after_blocklist_check_callback = base::BindOnce(
      &FileSystemAccessPermissionContext::DidCheckPathAgainstBlocklist,
      GetWeakPtr(), origin, path, handle_type, user_action, frame_id,
      std::move(callback));
  CheckPathAgainstBlocklist(path_type, path, handle_type,
                            std::move(after_blocklist_check_callback));
}

void FileSystemAccessPermissionContext::CheckPathAgainstBlocklist(
    PathType path_type,
    const base::FilePath& path,
    HandleType handle_type,
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // TODO(https://crbug.com/1009970): Figure out what external paths should be
  // blocked. We could resolve the external path to a local path, and check for
  // blocked directories based on that, but that doesn't work well. Instead we
  // should have a separate Chrome OS only code path to block for example the
  // root of certain external file systems.
  if (path_type == PathType::kExternal) {
    std::move(callback).Run(/*should_block=*/false);
    return;
  }

  std::vector<BlockPathRule> extra_rules;
  extra_rules.emplace_back(browser_context_->GetPath().DirName(),
                           BlockType::kBlockAllChildren);

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ShouldBlockAccessToPath, path, handle_type, extra_rules),
      std::move(callback));
}

void FileSystemAccessPermissionContext::PerformAfterWriteChecks(
    std::unique_ptr<content::FileSystemAccessWriteItem> item,
    content::GlobalRenderFrameHostId frame_id,
    base::OnceCallback<void(AfterWriteCheckResult)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::move(callback).Run(AfterWriteCheckResult::kAllow);
}

void FileSystemAccessPermissionContext::DidCheckPathAgainstBlocklist(
    const url::Origin& origin,
    const base::FilePath& path,
    HandleType handle_type,
    UserAction user_action,
    content::GlobalRenderFrameHostId frame_id,
    base::OnceCallback<void(SensitiveEntryResult)> callback,
    bool should_block) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (user_action == UserAction::kNone) {
    std::move(callback).Run(should_block ? SensitiveEntryResult::kAbort
                                         : SensitiveEntryResult::kAllowed);
    return;
  }

  // Chromium opens a dialog here, but in Electron's case we log and abort.
  if (should_block) {
    LOG(INFO) << path.value()
              << " is blocked by the blocklis and cannot be accessed";
    std::move(callback).Run(SensitiveEntryResult::kAbort);
    return;
  }

  std::move(callback).Run(SensitiveEntryResult::kAllowed);
}

void FileSystemAccessPermissionContext::SetLastPickedDirectory(
    const url::Origin& origin,
    const std::string& id,
    const base::FilePath& path,
    const PathType type) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  LOG(INFO) << "NOTIMPLEMENTED SetLastPickedDirectory: " << path.value();
}

FileSystemAccessPermissionContext::PathInfo
FileSystemAccessPermissionContext::GetLastPickedDirectory(
    const url::Origin& origin,
    const std::string& id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  LOG(INFO) << "NOTIMPLEMENTED GetLastPickedDirectory";
  return PathInfo();
}

base::FilePath FileSystemAccessPermissionContext::GetWellKnownDirectoryPath(
    blink::mojom::WellKnownDirectory directory,
    const url::Origin& origin) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  int key = base::PATH_START;
  switch (directory) {
    case blink::mojom::WellKnownDirectory::kDirDesktop:
      key = base::DIR_USER_DESKTOP;
      break;
    case blink::mojom::WellKnownDirectory::kDirDocuments:
      key = chrome::DIR_USER_DOCUMENTS;
      break;
    case blink::mojom::WellKnownDirectory::kDirDownloads:
      key = chrome::DIR_DEFAULT_DOWNLOADS;
      break;
    case blink::mojom::WellKnownDirectory::kDirMusic:
      key = chrome::DIR_USER_MUSIC;
      break;
    case blink::mojom::WellKnownDirectory::kDirPictures:
      key = chrome::DIR_USER_PICTURES;
      break;
    case blink::mojom::WellKnownDirectory::kDirVideos:
      key = chrome::DIR_USER_VIDEOS;
      break;
  }
  base::FilePath directory_path;
  base::PathService::Get(key, &directory_path);
  return directory_path;
}

std::u16string FileSystemAccessPermissionContext::GetPickerTitle(
    const blink::mojom::FilePickerOptionsPtr& options) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // TODO(asully): Consider adding custom strings for invocations of the file
  // picker, as well. Returning the empty string will fall back to the platform
  // default for the given picker type.
  std::u16string title;
  switch (options->type_specific_options->which()) {
    case blink::mojom::TypeSpecificFilePickerOptionsUnion::Tag::
        kDirectoryPickerOptions:
      title = l10n_util::GetStringUTF16(
          options->type_specific_options->get_directory_picker_options()
                  ->request_writable
              ? IDS_FILE_SYSTEM_ACCESS_CHOOSER_OPEN_WRITABLE_DIRECTORY_TITLE
              : IDS_FILE_SYSTEM_ACCESS_CHOOSER_OPEN_READABLE_DIRECTORY_TITLE);
      break;
    case blink::mojom::TypeSpecificFilePickerOptionsUnion::Tag::
        kSaveFilePickerOptions:
      title = l10n_util::GetStringUTF16(
          IDS_FILE_SYSTEM_ACCESS_CHOOSER_OPEN_SAVE_FILE_TITLE);
      break;
    case blink::mojom::TypeSpecificFilePickerOptionsUnion::Tag::
        kOpenFilePickerOptions:
      break;
  }
  return title;
}

void FileSystemAccessPermissionContext::NotifyEntryMoved(
    const url::Origin& origin,
    const base::FilePath& old_path,
    const base::FilePath& new_path) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (old_path == new_path) {
    return;
  }

  auto it = active_permissions_map_.find(origin);
  if (it != active_permissions_map_.end()) {
    PermissionGrantImpl::UpdateGrantPath(it->second.write_grants, old_path,
                                         new_path);
    PermissionGrantImpl::UpdateGrantPath(it->second.read_grants, old_path,
                                         new_path);
  }
}

void FileSystemAccessPermissionContext::OnFileCreatedFromShowSaveFilePicker(
    const GURL& file_picker_binding_context,
    const storage::FileSystemURL& url) {}

void FileSystemAccessPermissionContext::CheckPathsAgainstEnterprisePolicy(
    std::vector<PathInfo> entries,
    content::GlobalRenderFrameHostId frame_id,
    EntriesAllowedByEnterprisePolicyCallback callback) {
  std::move(callback).Run(std::move(entries));
}

void FileSystemAccessPermissionContext::RevokeGrant(
    const url::Origin& origin,
    const base::FilePath& file_path) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto origin_it = active_permissions_map_.find(origin);
  if (origin_it != active_permissions_map_.end()) {
    OriginState& origin_state = origin_it->second;
    for (auto& grant : origin_state.read_grants) {
      if (file_path.empty() || grant.first == file_path) {
        grant.second->SetStatus(PermissionStatus::ASK);
      }
    }
    for (auto& grant : origin_state.write_grants) {
      if (file_path.empty() || grant.first == file_path) {
        grant.second->SetStatus(PermissionStatus::ASK);
      }
    }
  }
}

bool FileSystemAccessPermissionContext::OriginHasReadAccess(
    const url::Origin& origin) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = active_permissions_map_.find(origin);
  if (it != active_permissions_map_.end()) {
    return base::ranges::any_of(it->second.read_grants, [&](const auto& grant) {
      return grant.second->GetStatus() == PermissionStatus::GRANTED;
    });
  }

  return false;
}

bool FileSystemAccessPermissionContext::OriginHasWriteAccess(
    const url::Origin& origin) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto it = active_permissions_map_.find(origin);
  if (it != active_permissions_map_.end()) {
    return base::ranges::any_of(
        it->second.write_grants, [&](const auto& grant) {
          return grant.second->GetStatus() == PermissionStatus::GRANTED;
        });
  }

  return false;
}

void FileSystemAccessPermissionContext::CleanupPermissions(
    const url::Origin& origin) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  RevokeGrant(origin);
}

bool FileSystemAccessPermissionContext::AncestorHasActivePermission(
    const url::Origin& origin,
    const base::FilePath& path,
    GrantType grant_type) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = active_permissions_map_.find(origin);
  if (it == active_permissions_map_.end()) {
    return false;
  }
  const auto& relevant_grants = grant_type == GrantType::kWrite
                                    ? it->second.write_grants
                                    : it->second.read_grants;
  if (relevant_grants.empty()) {
    return false;
  }

  // Permissions are inherited from the closest ancestor.
  for (base::FilePath parent = path.DirName(); parent != parent.DirName();
       parent = parent.DirName()) {
    auto i = relevant_grants.find(parent);
    if (i != relevant_grants.end() && i->second &&
        i->second->GetStatus() == PermissionStatus::GRANTED) {
      return true;
    }
  }
  return false;
}

void FileSystemAccessPermissionContext::PermissionGrantDestroyed(
    PermissionGrantImpl* grant) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = active_permissions_map_.find(grant->origin());
  if (it == active_permissions_map_.end()) {
    return;
  }

  auto& grants = grant->type() == GrantType::kRead ? it->second.read_grants
                                                   : it->second.write_grants;
  auto grant_it = grants.find(grant->GetPath());
  // Any non-denied permission grants should have still been in our grants
  // list. If this invariant is violated we would have permissions that might
  // be granted but won't be visible in any UI because the permission context
  // isn't tracking them anymore.
  if (grant_it == grants.end()) {
    DCHECK_EQ(PermissionStatus::DENIED, grant->GetStatus());
    return;
  }

  // The grant in |grants| for this path might have been replaced with a
  // different grant. Only erase if it actually matches the grant that was
  // destroyed.
  if (grant_it->second == grant) {
    grants.erase(grant_it);
  }
}

base::WeakPtr<FileSystemAccessPermissionContext>
FileSystemAccessPermissionContext::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

}  // namespace electron
