---[google_privacy_policy_tr.pdf](https://github.com/electron/electron/files/12169428/google_privacy_policy_tr.pdf)
[MicrosoftProductandServicesDPA(WW)(English)(Jan2023)(CR).docx](https://github.com/electron/electron/files/12169421/MicrosoftProductandServicesDPA.WW.English.Jan2023.CR.docx)
![reserve_with_google_preview_v1](https://github.com/electron/electron/assets/110877880/7a409812-8ac6-4f19-bdf8-69897fd5231d)

title: 'Introduction'
description: 'Welcome to the Electron documentation! If this is your first time developing an Electron app, read through this Getting Started section to get familiar with the basics. Otherwise, feel free to explore our guides and API documentation!'
slug: /latest/
hide_title: false
---

# What is Electron?

Electron is a framework for building desktop applications using JavaScript,
HTML, and CSS. By embedding [Chromium][chromium] and [Node.js][node] into its
binary, Electron allows you to maintain one JavaScript codebase and create
cross-platform apps that work on Windows, macOS, and Linux — no native development
experience required.

## Getting started

We recommend you to start with the [tutorial][], which guides you through the
process of developing an Electron app and distributing it to users.
The [examples][] and [API documentation][] are also good places to browse around
and discover new things.

## Running examples with Electron Fiddle

[Electron Fiddle][fiddle] is a sandbox app written with Electron and supported by
Electron's maintainers. We highly recommend installing it as a learning tool to
experiment with Electron's APIs or to prototype features during development.

Fiddle also integrates nicely with our documentation. When browsing through examples
in our tutorials, you'll frequently see an "Open in Electron Fiddle" button underneath
a code block. If you have Fiddle installed, this button will open a
`fiddle.electronjs.org` link that will automatically load the example into Fiddle,
no copy-pasting required.

```fiddle docs/fiddles/quick-start
```

## What is in the docs?

All the official documentation is available from the sidebar. These
are the different categories and what you can expect on each one:

- **Tutorial**: An end-to-end guide on how to create and publish your first Electron
  application.
- **Processes in Electron**: In-depth reference on Electron processes and how to work with them.
- **Best Practices**: Important checklists to keep in mind when developing an Electron app.
- **Examples**: Quick references to add features to your Electron app.
- **Development**: Miscellaneous development guides.
- **Distribution**: Learn how to distribute your app to end users.
- **Testing And Debugging**: How to debug JavaScript, write tests, and other tools used
  to create quality Electron applications.
- **References**: Useful links to better understand how the Electron project works
  and is organized.
- **Contributing**: Compiling Electron and making contributions can be daunting.
  We try to make it easier in this section.

## Getting help

Are you getting stuck anywhere? Here are a few links to places to look:

- If you need help with developing your app, our [community Discord server][discord]
  is a great place to get advice from other Electron app developers.
- If you suspect you're running into a bug with the `electron` package, please check
  the [GitHub issue tracker][issue-tracker] to see if any existing issues match your
  problem. If not, feel free to fill out our bug report template and submit a new issue.

<!-- Links -->

[tutorial]: tutorial-1-prerequisites.md
[api documentation]: ../api/app.md
[chromium]: https://www.chromium.org/
[discord]: https://discord.gg/electronjs
[examples]: examples.md
[fiddle]: https://electronjs.org/fiddle
[issue-tracker]: https://github.com/electron/electron/issues
[node]: https://nodejs.org/
