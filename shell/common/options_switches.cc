// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/options_switches.h"

namespace electron {

namespace options {

const char kTitle[] = "title";
const char kIcon[] = "icon";
const char kFrame[] = "frame";
const char kShow[] = "show";
const char kCenter[] = "center";
const char kX[] = "x";
const char kY[] = "y";
const char kWidth[] = "width";
const char kHeight[] = "height";
const char kMinWidth[] = "minWidth";
const char kMinHeight[] = "minHeight";
const char kMaxWidth[] = "maxWidth";
const char kMaxHeight[] = "maxHeight";
const char kResizable[] = "resizable";
const char kMovable[] = "movable";
const char kMinimizable[] = "minimizable";
const char kMaximizable[] = "maximizable";
const char kFullScreenable[] = "fullscreenable";
const char kClosable[] = "closable";
const char kFullscreen[] = "fullscreen";
const char kTrafficLightPosition[] = "trafficLightPosition";
const char kRoundedCorners[] = "roundedCorners";

// The color to use as the theme and symbol colors respectively for Window
// Controls Overlay if enabled on Windows.
const char kOverlayButtonColor[] = "color";
const char kOverlaySymbolColor[] = "symbolColor";

// The custom height for Window Controls Overlay.
const char kOverlayHeight[] = "height";

// whether to keep the window out of mission control
const char kHiddenInMissionControl[] = "hiddenInMissionControl";
// Whether the window should show in taskbar.
const char kSkipTaskbar[] = "skipTaskbar";

// Start with the kiosk mode, see Opera's page for description:
// http://www.opera.com/support/mastering/kiosk/
const char kKiosk[] = "kiosk";

const char kSimpleFullScreen[] = "simpleFullscreen";

// Make windows stays on the top of all other windows.
const char kAlwaysOnTop[] = "alwaysOnTop";

// Enable the NSView to accept first mouse event.
const char kAcceptFirstMouse[] = "acceptFirstMouse";

// Whether window size should include window frame.
const char kUseContentSize[] = "useContentSize";

// Whether window zoom should be to page width.
const char kZoomToPageWidth[] = "zoomToPageWidth";

// The requested title bar style for the window
const char kTitleBarStyle[] = "titleBarStyle";

// Tabbing identifier for the window if native tabs are enabled on macOS.
const char kTabbingIdentifier[] = "tabbingIdentifier";

// The menu bar is hidden unless "Alt" is pressed.
const char kAutoHideMenuBar[] = "autoHideMenuBar";

// Enable window to be resized larger than screen.
const char kEnableLargerThanScreen[] = "enableLargerThanScreen";

// Forces to use dark theme on Linux.
const char kDarkTheme[] = "darkTheme";

// Whether the window should be transparent.
const char kTransparent[] = "transparent";

// Window type hint.
const char kType[] = "type";

// Disable auto-hiding cursor.
const char kDisableAutoHideCursor[] = "disableAutoHideCursor";

// Use the macOS' standard window instead of the textured window.
const char kStandardWindow[] = "standardWindow";

// Default browser window background color.
const char kBackgroundColor[] = "backgroundColor";

// Whether the window should have a shadow.
const char kHasShadow[] = "hasShadow";

// Browser window opacity
const char kOpacity[] = "opacity";

// Whether the window can be activated.
const char kFocusable[] = "focusable";

// The WebPreferences.
const char kWebPreferences[] = "webPreferences";

// Add a vibrancy effect to the browser window
const char kVibrancyType[] = "vibrancy";

// Add a vibrancy effect to the browser window.
const char kBackgroundMaterial[] = "backgroundMaterial";

// Specify how the material appearance should reflect window activity state on
// macOS.
const char kVisualEffectState[] = "visualEffectState";

// The factor of which page should be zoomed.
const char kZoomFactor[] = "zoomFactor";

// Script that will be loaded by guest WebContents before other scripts.
const char kPreloadScript[] = "preload";

// Enable the node integration.
const char kNodeIntegration[] = "nodeIntegration";

// Enable context isolation of Electron APIs and preload script
const char kContextIsolation[] = "contextIsolation";

// Web runtime features.
const char kExperimentalFeatures[] = "experimentalFeatures";

// Enable the rubber banding effect.
const char kScrollBounce[] = "scrollBounce";

// Enable blink features.
const char kEnableBlinkFeatures[] = "enableBlinkFeatures";

// Disable blink features.
const char kDisableBlinkFeatures[] = "disableBlinkFeatures";

// Enable the node integration in WebWorker.
const char kNodeIntegrationInWorker[] = "nodeIntegrationInWorker";

// Enable the web view tag.
const char kWebviewTag[] = "webviewTag";

const char kCustomArgs[] = "additionalArguments";

const char kPlugins[] = "plugins";

const char kSandbox[] = "sandbox";

const char kWebSecurity[] = "webSecurity";

const char kAllowRunningInsecureContent[] = "allowRunningInsecureContent";

const char kOffscreen[] = "offscreen";

const char kNodeIntegrationInSubFrames[] = "nodeIntegrationInSubFrames";

// Disable window resizing when HTML Fullscreen API is activated.
const char kDisableHtmlFullscreenWindowResize[] =
    "disableHtmlFullscreenWindowResize";

// Enables JavaScript support.
const char kJavaScript[] = "javascript";

// Enables image support.
const char kImages[] = "images";

// Make TextArea elements resizable.
const char kTextAreasAreResizable[] = "textAreasAreResizable";

// Enables WebGL support.
const char kWebGL[] = "webgl";

// Whether dragging and dropping a file or link onto the page causes a
// navigation.
const char kNavigateOnDragDrop[] = "navigateOnDragDrop";

const char kHiddenPage[] = "hiddenPage";

#if BUILDFLAG(ENABLE_BUILTIN_SPELLCHECKER)
const char kSpellcheck[] = "spellcheck";
#endif

const char kEnablePreferredSizeMode[] = "enablePreferredSizeMode";

const char ktitleBarOverlay[] = "titleBarOverlay";

}  // namespace options

namespace switches {

// Enable chromium sandbox.
const char kEnableSandbox[] = "enable-sandbox";

// Ppapi Flash path.
const char kPpapiFlashPath[] = "ppapi-flash-path";

// Ppapi Flash version.
const char kPpapiFlashVersion[] = "ppapi-flash-version";

// Disable HTTP cache.
const char kDisableHttpCache[] = "disable-http-cache";

// The list of standard schemes.
const char kStandardSchemes[] = "standard-schemes";

// Register schemes to handle service worker.
const char kServiceWorkerSchemes[] = "service-worker-schemes";

// Register schemes as secure.
const char kSecureSchemes[] = "secure-schemes";

// Register schemes as bypassing CSP.
const char kBypassCSPSchemes[] = "bypasscsp-schemes";

// Register schemes as support fetch API.
const char kFetchSchemes[] = "fetch-schemes";

// Register schemes as CORS enabled.
const char kCORSSchemes[] = "cors-schemes";

// Register schemes as streaming responses.
const char kStreamingSchemes[] = "streaming-schemes";

// Register schemes as supporting V8 code cache.
const char kCodeCacheSchemes[] = "code-cache-schemes";

// The browser process app model ID
const char kAppUserModelId[] = "app-user-model-id";

// The application path
const char kAppPath[] = "app-path";

// The command line switch versions of the options.
const char kScrollBounce[] = "scroll-bounce";

// Command switch passed to renderer process to control nodeIntegration.
const char kNodeIntegrationInWorker[] = "node-integration-in-worker";

// Widevine options
// Path to Widevine CDM binaries.
const char kWidevineCdmPath[] = "widevine-cdm-path";
// Widevine CDM version.
const char kWidevineCdmVersion[] = "widevine-cdm-version";

// Forces the maximum disk space to be used by the disk cache, in bytes.
const char kDiskCacheSize[] = "disk-cache-size";

// Ignore the limit of 6 connections per host.
const char kIgnoreConnectionsLimit[] = "ignore-connections-limit";

// Whitelist containing servers for which Integrated Authentication is enabled.
const char kAuthServerWhitelist[] = "auth-server-whitelist";

// Whitelist containing servers for which Kerberos delegation is allowed.
const char kAuthNegotiateDelegateWhitelist[] =
    "auth-negotiate-delegate-whitelist";

// If set, include the port in generated Kerberos SPNs.
const char kEnableAuthNegotiatePort[] = "enable-auth-negotiate-port";

// If set, NTLM v2 is disabled for POSIX platforms.
const char kDisableNTLMv2[] = "disable-ntlm-v2";

}  // namespace switches

}  // namespace electron
