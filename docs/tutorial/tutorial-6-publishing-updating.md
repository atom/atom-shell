---
title: 'Publishing and Updating'
description: "There are several ways to update an Electron application. The easiest and officially supported one is taking advantage of the built-in Squirrel framework and Electron's autoUpdater module."
slug: tutorial-publishing-updating
hide_title: false
---

:::info Follow along the tutorial

This is **part 6** of the Electron tutorial.

1. [Prerequisites][prerequisites]
1. [Building your First App][building your first app]
1. [Using Preload Scripts][preload]
1. [Adding Features][features]
1. [Packaging Your Application][packaging]
1. **[Publishing and Updating][updates]**

:::

## Learning goals

If you've been following along, this is the last step of the tutorial! In this part,
you will publish your app to GitHub releases and integrate automatic updates
into your app code.

## Using update.electronjs.org

The Electron maintainers provide a free auto-updating service for open-source apps
at [https://update.electronjs.org](https://update.electronjs.org). Its requirements are:

- Your app runs on macOS or Windows
- Your app has a public GitHub repository
- Builds are published to [GitHub releases][]
- Builds are [code signed][code-signed]

At this point, we'll assume that you have already pushed all your
code to a public GitHub repository.

:::info Alternative update services

If you're using an alternate repository host (e.g. GitLab or Bitbucket) or if
you need to keep your code repository private, please refer to our
[step-by-step guide][update-server] on hosting your own Electron update server.

:::

## Publishing a GitHub release

Electron Forge has [Publisher][] plugins that can automate the distribution
of your packaged application to various sources. In this tutorial, we will
be using the GitHub Publisher, which will allow us to publish
our code to GitHub releases.

### Generating a personal access token

Forge cannot publish to any repository on GitHub without permission. You
need to pass in an authenticated token that gives Forge access to
your GitHub releases. The easiest way to do this is to
[create a new personal access token (PAT)][new-pat]
with the `public_repo` scope, which gives write access to your public repositories.
**Make sure to keep this token a secret.**

### Setting up the GitHub Publisher

#### Installing the module

Forge's [GitHub Publisher][] is a plugin that
needs to be installed in your project's `devDependencies`:

```sh npm2yarn
npm install --save-dev @electron-forge/publisher-github
```

#### Configuring the publisher in Forge

Once you have it installed, you need to set it up in your Forge
configuration. A full list of options is documented in the Forge's
[`PublisherGitHubConfig`][] API docs.

```js title='forge.config.js'
module.exports = {
  publishers: [
    {
      name: '@electron-forge/publisher-github',
      config: {
        repository: {
          owner: 'github-user-name',
          name: 'github-repo-name'
        },
        prerelease: false,
        draft: true
      }
    }
  ]
}
```

:::tip Drafting releases before publishing

Notice that you have configured Forge to publish your release as a draft.
This will allow you to see the release with its generated artifacts
without actually publishing it to your end users. You can manually
publish your releases via GitHub after writing release notes and
double-checking that your distributables work.

:::

#### Setting up your authentication token

You also need to make the Publisher aware of your authentication token.
By default, it will use the value stored in the `GITHUB_TOKEN` environment
variable.

### Running the publish command

Add Forge's [publish command][] to your npm scripts.

```json {6} title='package.json'
  //...
  "scripts": {
    "start": "electron-forge start",
    "package": "electron-forge package",
    "make": "electron-forge make",
    "publish": "electron-forge publish"
  },
  //...
```

This command will run your configured makers and publish the output distributables to a new
GitHub release.

```sh npm2yarn
npm run publish
```

By default, this will only publish a single distributable for your host operating system and
architecture. You can publish for different architectures by passing in the `--arch` flag to your
Forge commands.

The name of this release will correspond to the `version` field in your project's package.json file.

:::tip Tagging releases

Optionally, you can also [tag your releases in Git][git-tag] so that your
release is associated with a labeled point in your code history. npm comes
with a handy [`npm version`](https://docs.npmjs.com/cli/v8/commands/npm-version)
command that can handle the version bumping and tagging for you.

:::

#### Bonus: Publishing in GitHub Actions

Publishing locally can be painful, especially because you can only create distributables
for your host operating system (i.e. you can't publish a Windows `.exe` file from macOS).

A solution for this would be to publish your app via automation workflows
such as [GitHub Actions][], which can run tasks in the
cloud on Ubuntu, macOS, and Windows. This is the exact approach taken by [Electron Fiddle][].
You can refer to Fiddle's [Build and Release pipeline][fiddle-build]
and [Forge configuration][fiddle-forge-config]
for more details.

## Instrumenting your updater code

Now that we have a functional release system via GitHub releases, we now need to tell our
Electron app to download an update whenever a new release is out. Electron apps do this
via the [autoUpdater][] module, which reads from an update server feed to check if a new version
is available for download.

The update.electronjs.org service provides an updater-compatible feed. For example, Electron
Fiddle v0.28.0 will check the endpoint at https://update.electronjs.org/electron/fiddle/darwin/v0.28.0
to see if a newer GitHub release is available.

After your release is published to GitHub, the update.electronjs.org service should work
for your application. The only step left is to configure the feed with the autoUpdater module.

To make this process easier, the Electron team maintains the [`update-electron-app`][] module,
which sets up the autoUpdater boilerplate for update.electronjs.org in one function
call — no configuration required. This module will search for the update.electronjs.org
feed that matches your project's package.json `"repository"` field.

First, install the module as a runtime dependency.

```sh npm2yarn
npm install update-electron-app
```

Then, import the module and call it immediately in the main process.

```js title='main.js' @ts-nocheck
require('update-electron-app')()
```

And that is all it takes! Once your application is packaged, it will update itself for each new
GitHub release that you publish.

## Summary

In this tutorial, we configured Electron Forge's GitHub Publisher to upload your app's
distributables to GitHub releases. Since distributables cannot always be generated
between platforms, we recommend setting up your building and publishing flow
in a Continuous Integration pipeline if you do not have access to machines.

Electron applications can self-update by pointing the autoUpdater module to an update server feed.
update.electronjs.org is a free update server provided by Electron for open-source applications
published on GitHub releases. Configuring your Electron app to use this service is as easy as
installing and importing the `update-electron-app` module.

If your application is not eligible for update.electronjs.org, you should instead deploy your
own update server and configure the autoUpdater module yourself.

:::info 🌟 You're done!

From here, you have officially completed our tutorial to Electron. Feel free to explore the
rest of our docs and happy developing! If you have questions, please stop by our community
[Discord server][].

:::

[autoupdater]: ../api/auto-updater.md
[code-signed]: ./code-signing.md
[discord server]: https://discord.gg/electronjs
[electron fiddle]: https://www.electronjs.org/fiddle
[fiddle-build]: https://github.com/electron/fiddle/blob/main/.github/workflows/build.yaml
[fiddle-forge-config]: https://github.com/electron/fiddle/blob/main/forge.config.js
[github actions]: https://github.com/features/actions
[github publisher]: https://www.electronforge.io/config/publishers/github
[github releases]: https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository
[git-tag]: https://git-scm.com/book/en/v2/Git-Basics-Tagging
[new-pat]: https://github.com/settings/tokens/new
[publish command]: https://www.electronforge.io/cli#publish
[publisher]: https://www.electronforge.io/config/publishers
[`publishergithubconfig`]: https://js.electronforge.io/interfaces/_electron_forge_publisher_github.PublisherGitHubConfig.html
[`update-electron-app`]: https://github.com/electron/update-electron-app
[update-server]: ./updates.md

<!-- Tutorial links -->

[prerequisites]: tutorial-1-prerequisites.md
[building your first app]: tutorial-2-first-app.md
[preload]: tutorial-3-preload.md
[features]: tutorial-4-adding-features.md
[packaging]: tutorial-5-packaging.md
[updates]: tutorial-6-publishing-updating.md
