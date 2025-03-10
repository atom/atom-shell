import { sortMenuItems } from '@electron/internal/browser/api/menu-utils';
import { setApplicationMenuWasSet } from '@electron/internal/browser/default-menu';

import { BaseWindow, MenuItem, webContents, Menu as MenuType, MenuItemConstructorOptions } from 'electron/main';

const bindings = process._linkedBinding('electron_browser_menu');

const { Menu } = bindings as { Menu: typeof MenuType };
const checked = new WeakMap<MenuItem, boolean>();
let applicationMenu: MenuType | null = null;
let groupIdIndex = 0;

/* Instance Methods */

Menu.prototype._init = function () {
  this.commandsMap = {};
  this.groupsMap = {};
  this.items = [];
};

Menu.prototype._isCommandIdChecked = function (id) {
  const item = this.commandsMap[id];
  if (!item) return false;
  return item.getCheckStatus();
};

Menu.prototype._isCommandIdEnabled = function (id) {
  return this.commandsMap[id] ? this.commandsMap[id].enabled : false;
};
Menu.prototype._shouldCommandIdWorkWhenHidden = function (id) {
  return this.commandsMap[id] ? !!this.commandsMap[id].acceleratorWorksWhenHidden : false;
};
Menu.prototype._isCommandIdVisible = function (id) {
  return this.commandsMap[id] ? this.commandsMap[id].visible : false;
};

Menu.prototype._getAcceleratorForCommandId = function (id, useDefaultAccelerator) {
  const command = this.commandsMap[id];
  if (!command) return;
  if (command.accelerator != null) return command.accelerator;
  if (useDefaultAccelerator) return command.getDefaultRoleAccelerator();
};

Menu.prototype._shouldRegisterAcceleratorForCommandId = function (id) {
  return this.commandsMap[id] ? this.commandsMap[id].registerAccelerator : false;
};

if (process.platform === 'darwin') {
  Menu.prototype._getSharingItemForCommandId = function (id) {
    return this.commandsMap[id] ? this.commandsMap[id].sharingItem : null;
  };
}

Menu.prototype._executeCommand = function (event, id) {
  const command = this.commandsMap[id];
  if (!command) return;
  const focusedWindow = BaseWindow.getFocusedWindow();
  command.click(event, focusedWindow, webContents.getFocusedWebContents());
};

Menu.prototype._menuWillShow = function () {
  // Ensure radio groups have at least one menu item selected
  for (const id of Object.keys(this.groupsMap)) {
    const found = this.groupsMap[id].find(item => item.checked) || null;
    if (!found) checked.set(this.groupsMap[id][0], true);
  }
};

Menu.prototype.popup = function (options = {}) {
  if (options == null || typeof options !== 'object') {
    throw new TypeError('Options must be an object');
  }
  let { window, x, y, positioningItem, sourceType, callback } = options;

  // no callback passed
  if (!callback || typeof callback !== 'function') callback = () => {};

  // set defaults
  if (typeof x !== 'number') x = -1;
  if (typeof y !== 'number') y = -1;
  if (typeof positioningItem !== 'number') positioningItem = -1;
  if (typeof sourceType !== 'string' || !sourceType) sourceType = 'mouse';

  // find which window to use
  const wins = BaseWindow.getAllWindows();
  if (!wins || !wins.includes(window as any)) {
    window = BaseWindow.getFocusedWindow() as any;
    if (!window && wins && wins.length > 0) {
      window = wins[0] as any;
    }
    if (!window) {
      throw new Error('Cannot open Menu without a BaseWindow present');
    }
  }

  this.popupAt(window as unknown as BaseWindow, options.frame, x, y, positioningItem, sourceType, callback);
  return { browserWindow: window, x, y, position: positioningItem };
};

Menu.prototype.closePopup = function (window) {
  if (window instanceof BaseWindow) {
    this.closePopupAt(window.id);
  } else {
    // Passing -1 (invalid) would make closePopupAt close the all menu runners
    // belong to this menu.
    this.closePopupAt(-1);
  }
};

Menu.prototype.getMenuItemById = function (id) {
  const items = this.items;

  let found = items.find(item => item.id === id) || null;
  for (let i = 0; !found && i < items.length; i++) {
    const { submenu } = items[i];
    if (submenu) {
      found = submenu.getMenuItemById(id);
    }
  }
  return found;
};

Menu.prototype.append = function (item) {
  return this.insert(this.getItemCount(), item);
};

Menu.prototype.insert = function (pos, item) {
  if ((item ? item.constructor : undefined) !== MenuItem) {
    throw new TypeError('Invalid item');
  }

  if (pos < 0) {
    throw new RangeError(`Position ${pos} cannot be less than 0`);
  } else if (pos > this.getItemCount()) {
    throw new RangeError(`Position ${pos} cannot be greater than the total MenuItem count`);
  }

  // insert item depending on its type
  insertItemByType.call(this, item, pos);

  // set item properties
  if (item.sublabel) this.setSublabel(pos, item.sublabel);
  if (item.toolTip) this.setToolTip(pos, item.toolTip);
  if (item.icon) this.setIcon(pos, item.icon);
  if (item.role) this.setRole(pos, item.role);

  // Make menu accessible to items.
  item.overrideReadOnlyProperty('menu', this);

  // Remember the items.
  this.items.splice(pos, 0, item);
  this.commandsMap[item.commandId] = item;
};

Menu.prototype._callMenuWillShow = function () {
  if (this.delegate) this.delegate.menuWillShow(this);
  for (const item of this.items) {
    if (item.submenu) item.submenu._callMenuWillShow();
  }
};

/* Static Methods */

Menu.getApplicationMenu = () => applicationMenu;

Menu.sendActionToFirstResponder = bindings.sendActionToFirstResponder;

// set application menu with a preexisting menu
Menu.setApplicationMenu = function (menu: MenuType) {
  if (menu && menu.constructor !== Menu) {
    throw new TypeError('Invalid menu');
  }

  applicationMenu = menu;
  setApplicationMenuWasSet();

  if (process.platform === 'darwin') {
    if (!menu) return;
    menu._callMenuWillShow();
    bindings.setApplicationMenu(menu);
  } else {
    const windows = BaseWindow.getAllWindows();
    windows.map(w => w.setMenu(menu));
  }
};

Menu.buildFromTemplate = function (template) {
  if (!Array.isArray(template)) {
    throw new TypeError('Invalid template for Menu: Menu template must be an array');
  }

  if (!areValidTemplateItems(template)) {
    throw new TypeError('Invalid template for MenuItem: must have at least one of label, role or type');
  }

  const sorted = sortTemplate(template);
  const filtered = removeExtraSeparators(sorted);

  const menu = new Menu();
  for (const item of filtered) {
    if (item instanceof MenuItem) {
      menu.append(item);
    } else {
      menu.append(new MenuItem(item));
    }
  }

  return menu;
};

/* Helper Functions */

// validate the template against having the wrong attribute
function areValidTemplateItems (template: (MenuItemConstructorOptions | MenuItem)[]) {
  return template.every(item =>
    item != null &&
    typeof item === 'object' &&
    (Object.hasOwn(item, 'label') || Object.hasOwn(item, 'role') || item.type === 'separator'));
}

function sortTemplate (template: (MenuItemConstructorOptions | MenuItem)[]) {
  const sorted = sortMenuItems(template);
  for (const item of sorted) {
    if (Array.isArray(item.submenu)) {
      item.submenu = sortTemplate(item.submenu);
    }
  }
  return sorted;
}

// Search between separators to find a radio menu item and return its group id
function generateGroupId (items: (MenuItemConstructorOptions | MenuItem)[], pos: number) {
  if (pos > 0) {
    for (let idx = pos - 1; idx >= 0; idx--) {
      if (items[idx].type === 'radio') return (items[idx] as MenuItem).groupId;
      if (items[idx].type === 'separator') break;
    }
  } else if (pos < items.length) {
    for (let idx = pos; idx <= items.length - 1; idx++) {
      if (items[idx].type === 'radio') return (items[idx] as MenuItem).groupId;
      if (items[idx].type === 'separator') break;
    }
  }
  groupIdIndex += 1;
  return groupIdIndex;
}

function removeExtraSeparators (items: (MenuItemConstructorOptions | MenuItem)[]) {
  // fold adjacent separators together
  let ret = items.filter((e, idx, arr) => {
    if (e.visible === false) return true;
    return e.type !== 'separator' || idx === 0 || arr[idx - 1].type !== 'separator';
  });

  // remove edge separators
  ret = ret.filter((e, idx, arr) => {
    if (e.visible === false) return true;
    return e.type !== 'separator' || (idx !== 0 && idx !== arr.length - 1);
  });

  return ret;
}

function insertItemByType (this: MenuType, item: MenuItem, pos: number) {
  const types = {
    normal: () => this.insertItem(pos, item.commandId, item.label),
    checkbox: () => this.insertCheckItem(pos, item.commandId, item.label),
    separator: () => this.insertSeparator(pos),
    submenu: () => this.insertSubMenu(pos, item.commandId, item.label, item.submenu),
    radio: () => {
      // Grouping radio menu items
      item.overrideReadOnlyProperty('groupId', generateGroupId(this.items, pos));
      if (this.groupsMap[item.groupId] == null) {
        this.groupsMap[item.groupId] = [];
      }
      this.groupsMap[item.groupId].push(item);

      // Setting a radio menu item should flip other items in the group.
      checked.set(item, item.checked);
      Object.defineProperty(item, 'checked', {
        enumerable: true,
        get: () => checked.get(item),
        set: () => {
          for (const other of this.groupsMap[item.groupId]) {
            if (other !== item) checked.set(other, false);
          }
          checked.set(item, true);
        }
      });
      this.insertRadioItem(pos, item.commandId, item.label, item.groupId);
    }
  };
  types[item.type]();
}

module.exports = Menu;
