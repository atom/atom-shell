# Electron Releases

Electron frequently releases major versions alongside every other Chromium release.
This document focuses on the release cadence and version support policy.
For a more in-depth guide on our git branches and how Electron uses semantic versions,
check out our [Electron Versioning](./electron-versioning.md) doc.

## Timeline

| Electron | Alpha | Beta | Stable | EOL | Chrome | Node | Supported |
| ------- | ----- | ------- | ------ | ------ | ---- | ---- | ---- |
| 33.0.0 |  2024-Aug-22 | 2024-Sep-18 | 2024-Oct-15 | 2025-Apr-29 | M130 | TBD | ✅ |
| 32.0.0 |  2024-Jun-14 | 2024-Jul-24 | 2024-Aug-20 | 2025-Mar-04 | M128 | v20.16 | ✅ |
| 31.0.0 |  2024-Apr-18 | 2024-May-15 | 2024-Jun-11 | 2025-Jan-07 | M126 | v20.14 | ✅ |
| 30.0.0 |  2024-Feb-22 | 2024-Mar-20 | 2024-Apr-16 | 2024-Oct-15 | M124 | v20.11 | ✅ |
| 29.0.0 |  2023-Dec-07 | 2024-Jan-24 | 2024-Feb-20 | 2024-Aug-20 | M122 | v20.9 | 🚫 |
| 28.0.0 |  2023-Oct-11 | 2023-Nov-06 | 2023-Dec-05 | 2024-Jun-11 | M120 | v18.18 | 🚫 |
| 27.0.0 |  2023-Aug-17 | 2023-Sep-13 | 2023-Oct-10 | 2024-Apr-16 | M118 | v18.17 | 🚫 |
| 26.0.0 | 2023-Jun-01 | 2023-Jun-27 | 2023-Aug-15 | 2024-Feb-20 | M116 | v18.16 | 🚫 |
| 25.0.0 | 2023-Apr-10 | 2023-May-02 | 2023-May-30 | 2023-Dec-05 | M114 | v18.15 | 🚫 |
| 24.0.0 | 2023-Feb-09 | 2023-Mar-07 | 2023-Apr-04 | 2023-Oct-10 | M112 | v18.14 | 🚫 |
| 23.0.0 | 2022-Dec-01 | 2023-Jan-10 | 2023-Feb-07 | 2023-Aug-15 | M110 | v18.12 | 🚫 |
| 22.0.0 | 2022-Sep-29 | 2022-Oct-25 | 2022-Nov-29 | 2023-Oct-10 | M108 | v16.17 | 🚫 |
| 21.0.0 | 2022-Aug-04 | 2022-Aug-30 | 2022-Sep-27 | 2023-Apr-04 | M106 | v16.16 | 🚫 |
| 20.0.0 | 2022-May-26 | 2022-Jun-21 | 2022-Aug-02 | 2023-Feb-07 | M104 | v16.15 | 🚫 |
| 19.0.0 | 2022-Mar-31 | 2022-Apr-26 | 2022-May-24 | 2022-Nov-29 | M102 | v16.14 | 🚫 |
| 18.0.0 | 2022-Feb-03 | 2022-Mar-03 | 2022-Mar-29 | 2022-Sep-27 | M100 | v16.13 | 🚫 |
| 17.0.0 | 2021-Nov-18 | 2022-Jan-06 | 2022-Feb-01 | 2022-Aug-02 | M98 | v16.13 | 🚫 |
| 16.0.0 | 2021-Sep-23 | 2021-Oct-20 | 2021-Nov-16 | 2022-May-24 | M96 | v16.9 | 🚫 |
| 15.0.0 | 2021-Jul-20 | 2021-Sep-01 | 2021-Sep-21 | 2022-May-24 | M94 | v16.5 | 🚫 |
| 14.0.0 | -- | 2021-May-27 | 2021-Aug-31 | 2022-Mar-29 | M93 | v14.17 | 🚫 |
| 13.0.0 | -- | 2021-Mar-04 | 2021-May-25 | 2022-Feb-01 | M91 | v14.16 | 🚫 |
| 12.0.0 | -- | 2020-Nov-19 | 2021-Mar-02 | 2021-Nov-16 | M89 | v14.16 | 🚫 |
| 11.0.0 | -- | 2020-Aug-27 | 2020-Nov-17 | 2021-Aug-31 | M87 | v12.18 | 🚫 |
| 10.0.0 | -- | 2020-May-21 | 2020-Aug-25 | 2021-May-25 | M85 | v12.16 | 🚫 |
| 9.0.0 | -- | 2020-Feb-06 | 2020-May-19 | 2021-Mar-02 | M83 | v12.14 | 🚫 |
| 8.0.0 | -- | 2019-Oct-24 | 2020-Feb-04 | 2020-Nov-17 | M80 | v12.13 | 🚫 |
| 7.0.0 | -- | 2019-Aug-01 | 2019-Oct-22 | 2020-Aug-25 | M78 | v12.8 | 🚫 |
| 6.0.0 | -- | 2019-Apr-25 | 2019-Jul-30 | 2020-May-19 | M76 | v12.14.0 | 🚫 |
| 5.0.0 | -- | 2019-Jan-22 | 2019-Apr-23 | 2020-Feb-04 | M73 | v12.0 | 🚫 |
| 4.0.0 | -- | 2018-Oct-11 | 2018-Dec-20 | 2019-Oct-22 | M69 | v10.11 | 🚫 |
| 3.0.0 | -- | 2018-Jun-21 | 2018-Sep-18 | 2019-Jul-30 | M66 | v10.2 | 🚫 |
| 2.0.0 | -- | 2018-Feb-21 | 2018-May-01 | 2019-Apr-23 | M61 | v8.9 | 🚫 |

:::info Official support dates may change

Electron's official support policy is the latest 3 stable releases. Our stable
release and end-of-life dates are determined by Chromium, and may be subject to
change. While we try to keep our planned release and end-of-life dates frequently
updated here, future dates may change if affected by upstream scheduling changes,
and may not always be accurately reflected.

 See [Chromium's public release schedule](https://chromiumdash.appspot.com/schedule) for
 definitive information about Chromium's scheduled release dates.

 :::

**Notes:**

* The `-alpha.1`, `-beta.1`, and `stable` dates are our solid release dates.
* We strive for weekly alpha/beta releases, but we often release more than scheduled.
* All dates are our goals but there may be reasons for adjusting the stable deadline, such as security bugs.

**Historical changes:**

* Since Electron 5, Electron has been publicizing its release dates ([see blog post](https://www.electronjs.org/blog/electron-5-0-timeline)).
* Since Electron 6, Electron major versions have been targeting every other Chromium major version. Each Electron stable should happen on the same day as Chrome stable ([see blog post](https://www.electronjs.org/blog/12-week-cadence)).
* Since Electron 16, Electron has been releasing major versions on an 8-week cadence in accordance to Chrome's change to a 4-week release cadence ([see blog post](https://www.electronjs.org/blog/8-week-cadence)).
* Electron temporarily extended support for Electron 22 until October 10, 2023, to support an extended end-of-life for Windows 7/8/8.1

## Version support policy

The latest three _stable_ major versions are supported by the Electron team.
For example, if the latest release is 6.1.x, then the 5.0.x as well
as the 4.2.x series are supported. We only support the latest minor release
for each stable release series. This means that in the case of a security fix,
6.1.x will receive the fix, but we will not release a new version of 6.0.x.

The latest stable release unilaterally receives all fixes from `main`,
and the version prior to that receives the vast majority of those fixes
as time and bandwidth warrants. The oldest supported release line will receive
only security fixes directly.

### Chromium version support

:::info Chromium release schedule

Chromium's public release schedule is [here](https://chromiumdash.appspot.com/schedule).

:::

Electron targets Chromium even-number versions, releasing every 8 weeks in concert
with Chromium's 4-week release schedule. For example, Electron 26 uses Chromium 116, while Electron 27 uses Chromium 118.

### Node.js version support

Electron upgrades its `main` branch to even-number versions of Node.js when they enter Active LTS. The schedule
is as follows:

<img src="https://raw.githubusercontent.com/nodejs/Release/main/schedule.svg?sanitize=true" alt="Releases">

If Electron has recently updated its `main` branch to a new major version of Node.js, the next stable
branch to be cut will be released with the new version.

Stable release lines of Electron will receive minor and patch bumps of Node.js after they are released.
Patch bumps to Node.js will be released in patch releases of Electron, and minor bumps to Node.js will result in a minor release of Electron.
Security-only release branches will receive security-related changes from Node.js releases, but not the full release.

### Breaking API changes

When an API is changed or removed in a way that breaks existing functionality, the
previous functionality will be supported for a minimum of two major versions when
possible before being removed. For example, if a function takes three arguments,
and that number is reduced to two in major version 10, the three-argument version would
continue to work until, at minimum, major version 12. Past the minimum two-version
threshold, we will attempt to support backwards compatibility beyond two versions
until the maintainers feel the maintenance burden is too high to continue doing so.

### End-of-life

When a release branch reaches the end of its support cycle, the series
will be deprecated in NPM and a final end-of-support release will be
made. This release will add a warning to inform that an unsupported
version of Electron is in use.

These steps are to help app developers learn when a branch they're
using becomes unsupported, but without being excessively intrusive
to end users.

If an application has exceptional circumstances and needs to stay
on an unsupported series of Electron, developers can silence the
end-of-support warning by omitting the final release from the app's
`package.json` `devDependencies`. For example, since the 1-6-x series
ended with an end-of-support 1.6.18 release, developers could choose
to stay in the 1-6-x series without warnings with `devDependency` of
`"electron": 1.6.0 - 1.6.17`.
