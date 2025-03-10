import { globalShortcut } from 'electron/main';

import { expect } from 'chai';

import { singleModifierCombinations, doubleModifierCombinations } from './lib/accelerator-helpers';
import { ifdescribe } from './lib/spec-helpers';

ifdescribe(process.platform !== 'win32')('globalShortcut module', () => {
  beforeEach(() => {
    globalShortcut.unregisterAll();
  });

  it('can register and unregister single accelerators', () => {
    const combinations = [...singleModifierCombinations, ...doubleModifierCombinations];

    combinations.forEach((accelerator) => {
      expect(globalShortcut.isRegistered(accelerator)).to.be.false(`Initially registered for ${accelerator}`);

      globalShortcut.register(accelerator, () => { });
      expect(globalShortcut.isRegistered(accelerator)).to.be.true(`Registration failed for ${accelerator}`);

      globalShortcut.unregister(accelerator);
      expect(globalShortcut.isRegistered(accelerator)).to.be.false(`Unregistration failed for ${accelerator}`);

      globalShortcut.register(accelerator, () => { });
      expect(globalShortcut.isRegistered(accelerator)).to.be.true(`Re-registration failed for ${accelerator}`);

      globalShortcut.unregisterAll();
      expect(globalShortcut.isRegistered(accelerator)).to.be.false(`Re-unregistration failed for ${accelerator}`);
    });
  });

  it('can register and unregister multiple accelerators', () => {
    const accelerators = ['CmdOrCtrl+X', 'CmdOrCtrl+Y'];

    expect(globalShortcut.isRegistered(accelerators[0])).to.be.false('first initially unregistered');
    expect(globalShortcut.isRegistered(accelerators[1])).to.be.false('second initially unregistered');

    globalShortcut.registerAll(accelerators, () => {});

    expect(globalShortcut.isRegistered(accelerators[0])).to.be.true('first registration worked');
    expect(globalShortcut.isRegistered(accelerators[1])).to.be.true('second registration worked');

    globalShortcut.unregisterAll();

    expect(globalShortcut.isRegistered(accelerators[0])).to.be.false('first unregistered');
    expect(globalShortcut.isRegistered(accelerators[1])).to.be.false('second unregistered');
  });

  it('does not crash when registering media keys as global shortcuts', () => {
    const accelerators = [
      'VolumeUp',
      'VolumeDown',
      'VolumeMute',
      'MediaNextTrack',
      'MediaPreviousTrack',
      'MediaStop', 'MediaPlayPause'
    ];

    expect(() => {
      globalShortcut.registerAll(accelerators, () => {});
    }).to.not.throw();

    globalShortcut.unregisterAll();
  });
});
