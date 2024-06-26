/**
 * Create and minimally track guest windows at the direction of the renderer
 * (via window.open). Here, "guest" roughly means "child" — it's not necessarily
 * emblematic of its process status; both in-process (same-origin) and
 * out-of-process (cross-origin) are created here. "Embedder" roughly means
 * "parent."
 */
import { BrowserWindow } from 'electron/main';
import type { BrowserWindowConstructorOptions, Referrer, WebContents, LoadURLOptions } from 'electron/main';
import { parseFeatures } from '@electron/internal/browser/parse-features-string';

type PostData = LoadURLOptions['postData']
export type WindowOpenArgs = {
  url: string,
  frameName: string,
  features: string,
}

const frameNamesToWindow = new Map<string, WebContents>();
const registerFrameNameToGuestWindow = (name: string, webContents: WebContents) => frameNamesToWindow.set(name, webContents);
const unregisterFrameName = (name: string) => frameNamesToWindow.delete(name);
const getGuestWebContentsByFrameName = (name: string) => frameNamesToWindow.get(name);

/**
 * `openGuestWindow` is called to create and setup event handling for the new
 * window.
 */
export function openGuestWindow ({ embedder, guest, referrer, disposition, postData, overrideBrowserWindowOptions, windowOpenArgs, outlivesOpener, createWindow }: {
  embedder: WebContents,
  guest?: WebContents,
  referrer: Referrer,
  disposition: string,
  postData?: PostData,
  overrideBrowserWindowOptions?: BrowserWindowConstructorOptions,
  windowOpenArgs: WindowOpenArgs,
  outlivesOpener: boolean,
  createWindow?: Electron.CreateWindowFunction
}): void {
  const { url, frameName, features } = windowOpenArgs;
  const { options: parsedOptions } = parseFeatures(features);
  const browserWindowOptions = {
    show: true,
    width: 800,
    height: 600,
    ...parsedOptions,
    ...overrideBrowserWindowOptions
  };

  // To spec, subsequent window.open calls with the same frame name (`target` in
  // spec parlance) will reuse the previous window.
  // https://html.spec.whatwg.org/multipage/window-object.html#apis-for-creating-and-navigating-browsing-contexts-by-name
  const existingWebContents = getGuestWebContentsByFrameName(frameName);
  if (existingWebContents) {
    if (existingWebContents.isDestroyed()) {
      // FIXME(t57ser): The webContents is destroyed for some reason, unregister the frame name
      unregisterFrameName(frameName);
    } else {
      existingWebContents.loadURL(url);
      return;
    }
  }

  if (createWindow) {
    const webContents = createWindow({
      webContents: guest,
      ...browserWindowOptions
    });

    if (guest != null) {
      if (webContents !== guest) {
        throw new Error('Invalid webContents. Created window should be connected to webContents passed with options object.');
      }

      webContents.loadURL(url, {
        httpReferrer: referrer,
        ...(postData && {
          postData,
          extraHeaders: formatPostDataHeaders(postData as Electron.UploadRawData[])
        })
      });

      handleWindowLifecycleEvents({ embedder, frameName, guest, outlivesOpener });
    }

    return;
  }

  const window = new BrowserWindow({
    webContents: guest,
    ...browserWindowOptions
  });

  if (!guest) {
    // When we open a new window from a link (via OpenURLFromTab),
    // the browser process is responsible for initiating navigation
    // in the new window.
    window.loadURL(url, {
      httpReferrer: referrer,
      ...(postData && {
        postData,
        extraHeaders: formatPostDataHeaders(postData as Electron.UploadRawData[])
      })
    });
  }

  handleWindowLifecycleEvents({ embedder, frameName, guest: window.webContents, outlivesOpener });

  embedder.emit('did-create-window', window, { url, frameName, options: browserWindowOptions, disposition, referrer, postData });
}

/**
 * Manage the relationship between embedder window and guest window. When the
 * guest is destroyed, notify the embedder. When the embedder is destroyed, so
 * too is the guest destroyed; this is Electron convention and isn't based in
 * browser behavior.
 */
const handleWindowLifecycleEvents = function ({ embedder, guest, frameName, outlivesOpener }: {
  embedder: WebContents,
  guest: WebContents,
  frameName: string,
  outlivesOpener: boolean
}) {
  const closedByEmbedder = function () {
    guest.removeListener('destroyed', closedByUser);
    guest.destroy();
  };

  const closedByUser = function () {
    // Embedder might have been closed
    if (!embedder.isDestroyed() && !outlivesOpener) {
      embedder.removeListener('current-render-view-deleted' as any, closedByEmbedder);
    }
  };
  if (!outlivesOpener) {
    embedder.once('current-render-view-deleted' as any, closedByEmbedder);
  }
  guest.once('destroyed', closedByUser);

  if (frameName) {
    registerFrameNameToGuestWindow(frameName, guest);
    guest.once('destroyed', function () {
      unregisterFrameName(frameName);
    });
  }
};

// Security options that child windows will always inherit from parent windows
const securityWebPreferences: { [key: string]: boolean } = {
  contextIsolation: true,
  javascript: false,
  nodeIntegration: false,
  sandbox: true,
  webviewTag: false,
  nodeIntegrationInSubFrames: false,
  enableWebSQL: false
};

export function makeWebPreferences ({ embedder, secureOverrideWebPreferences = {}, insecureParsedWebPreferences: parsedWebPreferences = {} }: {
  embedder: WebContents,
  insecureParsedWebPreferences?: ReturnType<typeof parseFeatures>['webPreferences'],
  // Note that override preferences are considered elevated, and should only be
  // sourced from the main process, as they override security defaults. If you
  // have unvetted prefs, use parsedWebPreferences.
  secureOverrideWebPreferences?: BrowserWindowConstructorOptions['webPreferences'],
}) {
  const parentWebPreferences = embedder.getLastWebPreferences()!;
  const securityWebPreferencesFromParent = (Object.keys(securityWebPreferences).reduce((map, key) => {
    if (securityWebPreferences[key] === parentWebPreferences[key as keyof Electron.WebPreferences]) {
      (map as any)[key] = parentWebPreferences[key as keyof Electron.WebPreferences];
    }
    return map;
  }, {} as Electron.WebPreferences));

  return {
    ...parsedWebPreferences,
    // Note that order is key here, we want to disallow the renderer's
    // ability to change important security options but allow main (via
    // setWindowOpenHandler) to change them.
    ...securityWebPreferencesFromParent,
    ...secureOverrideWebPreferences
  };
}

function formatPostDataHeaders (postData: PostData) {
  if (!postData) return;

  const { contentType, boundary } = parseContentTypeFormat(postData);
  if (boundary != null) { return `content-type: ${contentType}; boundary=${boundary}`; }

  return `content-type: ${contentType}`;
}

const MULTIPART_CONTENT_TYPE = 'multipart/form-data';
const URL_ENCODED_CONTENT_TYPE = 'application/x-www-form-urlencoded';

// Figure out appropriate headers for post data.
export const parseContentTypeFormat = function (postData: Exclude<PostData, undefined>) {
  if (postData.length) {
    if (postData[0].type === 'rawData') {
      // For multipart forms, the first element will start with the boundary
      // notice, which looks something like `------WebKitFormBoundary12345678`
      // Note, this regex would fail when submitting a urlencoded form with an
      // input attribute of name="--theKey", but, uhh, don't do that?
      const postDataFront = postData[0].bytes.toString();
      const boundary = /^--.*[^-\r\n]/.exec(postDataFront);
      if (boundary) {
        return {
          boundary: boundary[0].substr(2),
          contentType: MULTIPART_CONTENT_TYPE
        };
      }
    }
  }
  // Either the form submission didn't contain any inputs (the postData array
  // was empty), or we couldn't find the boundary and thus we can assume this is
  // a key=value style form.
  return {
    contentType: URL_ENCODED_CONTENT_TYPE
  };
};
