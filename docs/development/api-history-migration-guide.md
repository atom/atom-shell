# Electron API History Migration Guide

This document demonstrates how to add API History blocks to existing APIs.

## API history information

Here are some resources you can use to find information on the history of an API:

### Breaking Changes

* [`breaking-changes.md`](../breaking-changes.md)

### Additions

* `git blame`
* [Release notes](https://github.com/electron/electron/releases/)
* [`electron-api-historian`](https://github.com/electron/electron-api-historian)

## Example

> [!NOTE]
> The associated API is already removed, we will ignore that for the purpose of
> this example.

If we search through [`breaking-changes.md`](../breaking-changes.md) we can find
[a function that was deprecated in Electron `25.0`](../breaking-changes.md#deprecated-browserwindowsettrafficlightpositionposition).

```markdown
<!-- docs/breaking-changes.md -->
### Deprecated: `BrowserWindow.getTrafficLightPosition()`

`BrowserWindow.getTrafficLightPosition()` has been deprecated, the
`BrowserWindow.getWindowButtonPosition()` API should be used instead
which returns `null` instead of `{ x: 0, y: 0 }` when there is no custom
position.

<!-- docs/api/browser-window.md  -->
#### `win.getTrafficLightPosition()` _macOS_ _Deprecated_

Returns `Point` - The custom position for the traffic light buttons in
frameless window, `{ x: 0, y: 0 }` will be returned when there is no custom
position.
```

We can then use `git blame` to find the Pull Request associated with that entry:

```bash
$ grep -n "BrowserWindow.getTrafficLightPosition" docs/breaking-changes.md 
523:### Deprecated: `BrowserWindow.getTrafficLightPosition()`
525:`BrowserWindow.getTrafficLightPosition()` has been deprecated, the

$ git blame -L523,524 -- docs/breaking-changes.md
1e206deec3e (Keeley Hammond 2023-04-06 21:23:29 -0700 523) ### Deprecated: `BrowserWindow.getTrafficLightPosition()`
1e206deec3e (Keeley Hammond 2023-04-06 21:23:29 -0700 524)

$ git log -1 1e206deec3e
commit 1e206deec3ef142460c780307752a84782f9baed (tag: v26.0.0-nightly.20230407)
Author: Keeley Hammond <vertedinde@electronjs.org>
Date:   Thu Apr 6 21:23:29 2023 -0700

    docs: update E24/E25 breaking changes (#37878) <-- This is the associated Pull Request
```

Verify that the Pull Request is correct and make a corresponding entry in the
API History:

> [!NOTE]
> Refer to the [API History section of `styleguide.md`](../styleguide.md#api-history)
for information on how to create API History blocks.

`````markdown
#### `win.getTrafficLightPosition()` _macOS_ _Deprecated_

<!--
```YAML history
deprecated:
  - pr-url: https://github.com/electron/electron/pull/37878
    breaking-changes-header: deprecated-browserwindowgettrafficlightposition
```
-->

Returns `Point` - The custom position for the traffic light buttons in
frameless window, `{ x: 0, y: 0 }` will be returned when there is no custom
position.
`````

You can keep looking through `breaking-changes.md` to find other breaking changes
and add those in.

You can also use [`git log -L :<funcname>:<file>`](https://git-scm.com/docs/git-log#Documentation/git-log.txt--Lltfuncnamegtltfilegt):

```bash
$ git log --reverse -L :GetTrafficLightPosition:shell/browser/native_window_mac.mm
commit e01b1831d96d5d68f54af879b00c617358df5372
Author: Cheng Zhao <zcbenz@gmail.com>
Date:   Wed Dec 16 14:30:39 2020 +0900

    feat: make trafficLightPosition work for customButtonOnHover (#26789)
```

Verify that the Pull Request is correct and make a corresponding entry in the
API History:

`````markdown
#### `win.getTrafficLightPosition()` _macOS_ _Deprecated_

<!--
```YAML history
added:
  - pr-url: https://github.com/electron/electron/pull/22533
changes:
  - pr-url: https://github.com/electron/electron/pull/26789
    description: "Made `trafficLightPosition` option work for `customButtonOnHover` window."
    breaking-changes-header: behavior-changed-draggable-regions-on-macos
```
-->

Returns `Point` - The custom position for the traffic light buttons in
frameless window, `{ x: 0, y: 0 }` will be returned when there is no custom
position.
`````

We will then look for when the API was originally added:

```bash
$ git log --reverse -L :GetTrafficLightPosition:shell/browser/native_window_mac.mm
commit 3e2cec83d927b991855e21cc311ca9046e332601
Author: Samuel Attard <sattard@slack-corp.com>
Date:   Thu Mar 5 14:22:12 2020 -0800

    feat: programmatically modify traffic light positioning (#22533)
```

Alternatively, you can use `git blame`:

```bash
$ git checkout 1e206deec3e^
HEAD is now at e8c87859c4 fix: showAboutPanel also on linux (#37828)

$ grep -n "getTrafficLightPosition" docs/api/browser-window.md
1867:#### `win.getTrafficLightPosition()` _macOS_ _Deprecated_

$ git blame -L1867,1868 -- docs/api/browser-window.md
0de1012280e (Cheng Zhao    2023-02-17 19:06:32 +0900 1867) #### `win.getTrafficLightPosition()` _macOS_ _Deprecated_
3e2cec83d92 (Samuel Attard 2020-03-05 14:22:12 -0800 1868) 

$ git checkout 0de1012280e^
HEAD is now at 0a5e634736 test: rename & split internal module tests (#37318)

$ grep -n "getTrafficLightPosition" docs/api/browser-window.md 
1851:#### `win.getTrafficLightPosition()` _macOS_

$ git blame -L1851,1852 -- docs/api/browser-window.md
3e2cec83d92 (Samuel Attard 2020-03-05 14:22:12 -0800 1851) #### `win.getTrafficLightPosition()` _macOS_
3e2cec83d92 (Samuel Attard 2020-03-05 14:22:12 -0800 1852)

$ git checkout 3e2cec83d92^
HEAD is now at 1811751c6c docs: clean up dark mode related docs (#22489)

$ grep -n "getTrafficLightPosition" docs/api/browser-window.md
(Nothing)

$ git checkout 3e2cec83d92
HEAD is now at 3e2cec83d9 feat: programmatically modify traffic light positioning (#22533)
```

Verify that the Pull Request is correct and make a corresponding entry in the
API History:

`````markdown
#### `win.getTrafficLightPosition()` _macOS_ _Deprecated_

<!--
```YAML history
added:
  - pr-url: https://github.com/electron/electron/pull/22533
changes:
  - pr-url: https://github.com/electron/electron/pull/26789
    description: "Made `trafficLightPosition` option work for `customButtonOnHover` window."
    breaking-changes-header: behavior-changed-draggable-regions-on-macos
deprecated:
  - pr-url: https://github.com/electron/electron/pull/37878
    breaking-changes-header: deprecated-browserwindowgettrafficlightposition
```
-->

Returns `Point` - The custom position for the traffic light buttons in
frameless window, `{ x: 0, y: 0 }` will be returned when there is no custom
position.
`````
