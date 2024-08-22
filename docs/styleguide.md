# Electron Documentation Style Guide

These are the guidelines for writing Electron documentation.

## Headings

* Each page must have a single `#`-level title at the top.
* Chapters in the same page must have `##`-level headings.
* Sub-chapters need to increase the number of `#` in the heading according to
  their nesting depth.
* The page's title must follow [APA title case][title-case].
* All chapters must follow [APA sentence case][sentence-case].

Using `Quick Start` as example:

```markdown
# Quick Start

...

## Main process

...

## Renderer process

...

## Run your app

...

### Run as a distribution

...

### Manually downloaded Electron binary

...
```

For API references, there are exceptions to this rule.

## Markdown rules

This repository uses the [`markdownlint`][markdownlint] package to enforce consistent
Markdown styling. For the exact rules, see the `.markdownlint.json` file in the root
folder.

There are a few style guidelines that aren't covered by the linter rules:

<!--TODO(erickzhao): make sure this matches with the lint:markdownlint task-->
* Use `sh` instead of `cmd` in code blocks (due to the syntax highlighter).
* Keep line lengths between 80 and 100 characters if possible for readability
  purposes.
* No nesting lists more than 2 levels (due to the markdown renderer).
* All `js` and `javascript` code blocks are linted with
[standard-markdown](https://www.npmjs.com/package/standard-markdown).
* For unordered lists, use asterisks instead of dashes.

## Picking words

* Use "will" over "would" when describing outcomes.
* Prefer "in the ___ process" over "on".

## API references

The following rules only apply to the documentation of APIs.

### Title and description

Each module's API doc must use the actual object name returned by `require('electron')`
as its title (such as `BrowserWindow`, `autoUpdater`, and `session`).

Directly under the page title, add a one-line description of the module
as a markdown quote (beginning with `>`).

Using the `session` module as an example:

```markdown
# session

> Manage browser sessions, cookies, cache, proxy settings, etc.
```

### Module methods and events

For modules that are not classes, their methods and events must be listed under
the `## Methods` and `## Events` chapters.

Using `autoUpdater` as an example:

```markdown
# autoUpdater

## Events

### Event: 'error'

## Methods

### `autoUpdater.setFeedURL(url[, requestHeaders])`
```

### Classes

* API classes or classes that are part of modules must be listed under a
  `## Class: TheClassName` chapter.
* One page can have multiple classes.
* Constructors must be listed with `###`-level headings.
* [Static Methods](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Classes/static)
  must be listed under a `### Static Methods` chapter.
* [Instance Methods](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Classes#Prototype_methods)
  must be listed under an `### Instance Methods` chapter.
* All methods that have a return value must start their description with
  "Returns `[TYPE]` - \[Return description]"
  * If the method returns an `Object`, its structure can be specified using a colon
    followed by a newline then an unordered list of properties in the same style as
    function parameters.
* Instance Events must be listed under an `### Instance Events` chapter.
* Instance Properties must be listed under an `### Instance Properties` chapter.
  * Instance Properties must start with "A \[Property Type] ..."

Using the `Session` and `Cookies` classes as an example:

```markdown
# session

## Methods

### session.fromPartition(partition)

## Static Properties

### session.defaultSession

## Class: Session

### Instance Events

#### Event: 'will-download'

### Instance Methods

#### `ses.getCacheSize()`

### Instance Properties

#### `ses.cookies`

## Class: Cookies

### Instance Methods

#### `cookies.get(filter, callback)`
```

### Methods and their arguments

The methods chapter must be in the following form:

```markdown
### `objectName.methodName(required[, optional]))`

* `required` string - A parameter description.
* `optional` Integer (optional) - Another parameter description.

...
```

#### Heading level

The heading can be `###` or `####`-levels depending on whether the method
belongs to a module or a class.

#### Function signature

For modules, the `objectName` is the module's name. For classes, it must be the
name of the instance of the class, and must not be the same as the module's
name.

For example, the methods of the `Session` class under the `session` module must
use `ses` as the `objectName`.

Optional arguments are notated by square brackets `[]` surrounding the optional
argument as well as the comma required if this optional argument follows another
argument:

```markdown
required[, optional]
```

#### Argument descriptions

More detailed information on each of the arguments is noted in an unordered list
below the method. The type of argument is notated by either JavaScript primitives
(e.g. `string`, `Promise`, or `Object`), a custom API structure like Electron's
[`Cookie`](api/structures/cookie.md), or the wildcard `any`.

If the argument is of type `Array`, use `[]` shorthand with the type of value
inside the array (for example,`any[]` or `string[]`).

If the argument is of type `Promise`, parametrize the type with what the promise
resolves to (for example, `Promise<void>` or `Promise<string>`).

If an argument can be of multiple types, separate the types with `|`.

The description for `Function` type arguments should make it clear how it may be
called and list the types of the parameters that will be passed to it.

#### Platform-specific functionality

If an argument or a method is unique to certain platforms, those platforms are
denoted using a space-delimited italicized list following the datatype. Values
can be `macOS`, `Windows` or `Linux`.

```markdown
* `animate` boolean (optional) _macOS_ _Windows_ - Animate the thing.
```

### Events

The events chapter must be in following form:

```markdown
### Event: 'wake-up'

Returns:

* `time` string

...
```

The heading can be `###` or `####`-levels depending on whether the event
belongs to a module or a class.

The arguments of an event follow the same rules as methods.

### Properties

The properties chapter must be in following form:

```markdown
### session.defaultSession

...
```

The heading can be `###` or `####`-levels depending on whether the property
belongs to a module or a class.

## API History

An "API History" block is a YAML code block encapsulated by an HTML comment that
should be placed directly after the Markdown header for a class or method, like so:

`````markdown
#### `win.setTrafficLightPosition(position)` _macOS_

<!--
```YAML history
added:
  - pr-url: https://github.com/electron/electron/pull/22533
changes:
  - pr-url: https://github.com/electron/electron/pull/26789
    description: "Made `trafficLightPosition` option work for `customButtonOnHover` window."
deprecated:
  - pr-url: https://github.com/electron/electron/pull/37094
    breaking-changes-header: deprecated-browserwindowsettrafficlightpositionposition
```
-->

* `position` [Point](structures/point.md)

Set a custom position for the traffic light buttons. Can only be used with `titleBarStyle` set to `hidden`.
`````

It should adhere to the API History [JSON Schema](https://json-schema.org/)
(`api-history.schema.json`) which you can find in the `docs` folder.
The [API History Schema RFC][api-history-schema-rfc] includes example usage and detailed
explanations for each aspect of the schema.

The purpose of the API History block is to describe when/where/how/why an API was:

* Added
* Changed (usually breaking changes)
* Deprecated

Each API change listed in the block should include a link to the
PR where that change was made along with an optional short description of the
change. If applicable, include the [heading id](https://gist.github.com/asabaylus/3071099)
for that change from the [breaking changes documentation](./breaking-changes.md).

The [API History linting script][api-history-linting-script] (`lint:api-history`)
validates API History blocks in the Electron documentation against the schema and
performs some other checks. You can look at its [tests][api-history-tests] for more
details.

There are a few style guidelines that aren't covered by the linting script:

### Format

Always adhere to this format:

  ```markdown
  API HEADER                  |  #### `win.flashFrame(flag)`
  BLANK LINE                  | 
  HTML COMMENT OPENING TAG    |  <!--
  API HISTORY OPENING TAG     |  ```YAML history
  API HISTORY                 |  added:
                              |    - pr-url: https://github.com/electron/electron/pull/22533
  API HISTORY CLOSING TAG     |  ```
  HTML COMMENT CLOSING TAG    |  -->
  BLANK LINE                  |
  ```

### YAML

* Use two spaces for indentation.
* Do not use comments.

### Descriptions

* Always wrap descriptions with double quotation marks (i.e. "example").
  * [Certain special characters (e.g. `[`, `]`) can break YAML parsing](https:/stackoverflow.com/a/37015689/19020549).
* Describe the change in a way relevant to app developers and make it
  capitalized, punctuated, and past tense.
  * Refer to [Clerk](https://github.com/electron/clerk/blob/main/README.md#examples)
    for examples.
* Keep descriptions concise.
  * Ideally, a description will match its corresponding header in the
    breaking changes document.
  * Favor using the release notes from the associated PR whenever possible.
  * Developers can always view the breaking changes document or linked
    pull request for more details.

### Placement

Generally, you should place the API History block directly after the Markdown header
for a class or method that was changed. However, there are some instances where this
is ambiguous:

#### Chromium bump

* [chore: bump chromium to 122.0.6194.0 (main)](https://github.com/electron/electron/pull/40750)
  * [Behavior Changed: cross-origin iframes now use Permission Policy to access features][api-history-cross-origin]

Sometimes a breaking change doesn't relate to any of the existing APIs. In this
case, it is ok not to add API History anywhere.

#### Change affecting multiple APIs

* [refactor: ensure IpcRenderer is not bridgable](https://github.com/electron/electron/pull/40330)
  * [Behavior Changed: ipcRenderer can no longer be sent over the contextBridge][api-history-ipc-renderer]

Sometimes a breaking change involves multiple APIs. In this case, place the
API History block under the top-level Markdown header for each of the
involved APIs.

`````markdown
# contextBridge

<!--
```YAML history
changes:
  - pr-url: https://github.com/electron/electron/pull/40330
    description: "`ipcRenderer` can no longer be sent over the `contextBridge`"
    breaking-changes-header: behavior-changed-ipcrenderer-can-no-longer-be-sent-over-the-contextbridge
```
-->

> Create a safe, bi-directional, synchronous bridge across isolated contexts
`````

`````markdown
# ipcRenderer

<!--
```YAML history
changes:
  - pr-url: https://github.com/electron/electron/pull/40330
    description: "`ipcRenderer` can no longer be sent over the `contextBridge`"
    breaking-changes-header: behavior-changed-ipcrenderer-can-no-longer-be-sent-over-the-contextbridge
```
-->

Process: [Renderer](../glossary.md#renderer-process)
`````

Notice how an API History block wasn't added under:

* `contextBridge.exposeInMainWorld(apiKey, api)`

since that function wasn't changed, only how it may be used:

```patch
  contextBridge.exposeInMainWorld('app', {
-   ipcRenderer,
+   onEvent: (cb) => ipcRenderer.on('foo', (e, ...args) => cb(args))
  })
```

## Documentation translations

See [electron/i18n](https://github.com/electron/i18n#readme)

[title-case]: https://apastyle.apa.org/style-grammar-guidelines/capitalization/title-case
[sentence-case]: https://apastyle.apa.org/style-grammar-guidelines/capitalization/sentence-case
[markdownlint]: https://github.com/DavidAnson/markdownlint
[api-history-schema-rfc]: https://github.com/electron/rfcs/blob/f36e0a8483e1ea844710890a8a7a1bd58ecbac05/text/0004-api-history-schema.md
[api-history-linting-script]: https://github.com/electron/lint-roller/blob/3030970136ec6b41028ef973f944d3e5cad68e1c/bin/lint-markdown-api-history.ts
[api-history-tests]: https://github.com/electron/lint-roller/blob/main/tests/lint-roller-markdown-api-history.spec.ts
[api-history-cross-origin]: https://github.com/electron/electron/blob/f508f6b6b570481a2b61d8c4f8c1951f492e4309/docs/breaking-changes.md#behavior-changed-cross-origin-iframes-now-use-permission-policy-to-access-features
[api-history-ipc-renderer]: https://github.com/electron/electron/blob/f508f6b6b570481a2b61d8c4f8c1951f492e4309/docs/breaking-changes.md#behavior-changed-ipcrenderer-can-no-longer-be-sent-over-the-contextbridge
