/**
 * @fileoverview A set of helper functions to make it easier to work
 * with accelerators across tests.
 */

const modifiers = [
  'CmdOrCtrl',
  'Alt',
  process.platform === 'darwin' ? 'Option' : null,
  'AltGr',
  'Shift',
  'Super',
  'Meta'
].filter(Boolean);

const keyCodes = [
  ...Array.from({ length: 10 }, (_, i) => `${i}`), // 0 to 9
  ...Array.from({ length: 26 }, (_, i) => String.fromCharCode(65 + i)), // A to Z
  ...Array.from({ length: 24 }, (_, i) => `F${i + 1}`), // F1 to F24
  ')', '!', '@', '#', '$', '%', '^', '&', '*', '(', ':', ';', ':', '+', '=',
  '<', ',', '_', '-', '>', '.', '?', '/', '~', '`', '{', ']', '[', '|', '\\',
  '}', '"', 'Plus', 'Space', 'Tab', 'Capslock', 'Numlock', 'Scrolllock',
  'Backspace', 'Delete', 'Insert', 'Return', 'Enter', 'Up', 'Down', 'Left',
  'Right', 'Home', 'End', 'PageUp', 'PageDown', 'Escape', 'Esc', 'PrintScreen',
  'num0', 'num1', 'num2', 'num3', 'num4', 'num5', 'num6', 'num7', 'num8', 'num9',
  'numdec', 'numadd', 'numsub', 'nummult', 'numdiv'
];

export const singleModifierCombinations = modifiers.flatMap(
  mod => keyCodes.map(key => {
    return key === '+' ? `${mod}+Plus` : `${mod}+${key}`;
  })
);

export const doubleModifierCombinations = modifiers.flatMap(
  (mod1, i) => modifiers.slice(i + 1).flatMap(
    mod2 => keyCodes.map(key => {
      return key === '+' ? `${mod1}+${mod2}+Plus` : `${mod1}+${mod2}+${key}`;
    })
  )
);
