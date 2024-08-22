## Class: NavigationHistory

> Manage a list of navigation entries, representing the user's browsing history within the application.

Process: [Main](../glossary.md#main-process)<br />
_This class is not exported from the `'electron'` module. It is only available as a return value of other methods in the Electron API._

Each navigation entry corresponds to a specific page. The indexing system follows a sequential order, where the first available navigation entry is at index 0, representing the earliest visited page, and the latest navigation entry is at index N, representing the most recent page. Maintaining this ordered list of navigation entries enables seamless navigation both backward and forward through the user's browsing history.

### Instance Methods

#### `navigationHistory.canGoBack()`

Returns `boolean` - Whether the browser can go back to previous web page.

#### `navigationHistory.canGoForward()`

Returns `boolean` - Whether the browser can go forward to next web page.

#### `navigationHistory.canGoToOffset(offset)`

* `offset` Integer

Returns `boolean` - Whether the web page can go to the specified `offset` from the current entry.

#### `navigationHistory.clear()`

Clears the navigation history.

#### `navigationHistory.getActiveIndex()`

Returns `Integer` - The index of the current page, from which we would go back/forward or reload.

#### `navigationHistory.getEntryAtIndex(index)`

* `index` Integer

Returns [`NavigationEntry`](structures/navigation-entry.md) - Navigation entry at the given index.

If index is out of bounds (greater than history length or less than 0), null will be returned.

#### `navigationHistory.goBack()`

Makes the browser go back a web page.

#### `navigationHistory.goForward()`

Makes the browser go forward a web page.

#### `navigationHistory.goToIndex(index)`

* `index` Integer

Navigates browser to the specified absolute web page index.

#### `navigationHistory.goToOffset(offset)`

* `offset` Integer

Navigates to the specified offset from the current entry.

#### `navigationHistory.length()`

Returns `Integer` - History length.

#### `navigationHistory.removeEntryAtIndex(index)`

* `index` Integer

Removes the navigation entry at the given index. Can't remove entry at the "current active index".

Returns `boolean` - Whether the navigation entry was removed from the webContents history.

#### `navigationHistory.getAllEntries()`

Returns [`NavigationEntry[]`](structures/navigation-entry.md) - WebContents complete history.
