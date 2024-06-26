---
title: 'Application Packaging'
description: 'To distribute your app with Electron, you need to package and rebrand it. To do this, you can either use specialized tooling or manual approaches.'
slug: application-distribution
hide_title: false
---

To distribute your app with Electron, you need to package and rebrand it. To do this, you
can either use specialized tooling or manual approaches.

## With tooling

There are a couple tools out there that exist to package and distribute your Electron app.
We recommend using [Electron Forge](./forge-overview.md). You can check out
its [documentation](https://www.electronforge.io) directly, or refer to the [Packaging and Distribution](./tutorial-5-packaging.md)
part of the Electron tutorial.

## Manual packaging

If you prefer the manual approach, there are 2 ways to distribute your application:

- With prebuilt binaries
- With an app source code archive

### With prebuilt binaries

To distribute your app manually, you need to download Electron's
[prebuilt binaries](https://github.com/electron/electron/releases). Next, the folder
containing your app should be named `app` and placed in Electron's resources
directory as shown in the following examples.

:::note
The location of Electron's prebuilt binaries is indicated
with `electron/` in the examples below.
:::

```plain title='macOS'
electron/Electron.app/Contents/Resources/app/
├── package.json
├── main.js
└── index.html
```

```plain title='Windows and Linux'
electron/resources/app
├── package.json
├── main.js
└── index.html
```

Then execute `Electron.app` on macOS, `electron` on Linux, or `electron.exe`
on Windows, and Electron will start as your app. The `electron` directory
will then be your distribution to deliver to users.

### With an app source code archive (asar)

Instead of shipping your app by copying all of its source files, you can
package your app into an [asar][] archive to improve the performance of reading
files on platforms like Windows, if you are not already using a bundler such
as Parcel or Webpack.

To use an `asar` archive to replace the `app` folder, you need to rename the
archive to `app.asar`, and put it under Electron's resources directory like
below, and Electron will then try to read the archive and start from it.

```plain title='macOS'
electron/Electron.app/Contents/Resources/
└── app.asar
```

```plain title='Windows'
electron/resources/
└── app.asar
```

You can find more details on how to use `asar` in the
[`electron/asar` repository][asar].

### Rebranding with downloaded binaries

After bundling your app into Electron, you will want to rebrand Electron
before distributing it to users.

- **Windows:** You can rename `electron.exe` to any name you like, and edit
  its icon and other information with tools like [rcedit](https://github.com/electron/rcedit).
- **Linux:** You can rename the `electron` executable to any name you like.
- **macOS:** You can rename `Electron.app` to any name you want, and you also have to rename
  the `CFBundleDisplayName`, `CFBundleIdentifier` and `CFBundleName` fields in the
  following files:

  - `Electron.app/Contents/Info.plist`
  - `Electron.app/Contents/Frameworks/Electron Helper.app/Contents/Info.plist`

  You can also rename the helper app to avoid showing `Electron Helper` in the
  Activity Monitor, but make sure you have renamed the helper app's executable
  file's name.

  The structure of a renamed app would be like:

```plain
MyApp.app/Contents
├── Info.plist
├── MacOS/
│   └── MyApp
└── Frameworks/
    └── MyApp Helper.app
        ├── Info.plist
        └── MacOS/
            └── MyApp Helper
```

:::note

it is also possible to rebrand Electron by changing the product name and
building it from source. To do this you need to set the build argument
corresponding to the product name (`electron_product_name = "YourProductName"`)
in the `args.gn` file and rebuild.

Keep in mind this is not recommended as setting up the environment to compile
from source is not trivial and takes significant time.

:::

[asar]: https://github.com/electron/asar
