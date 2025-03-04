import { screen, desktopCapturer, NativeImage } from 'electron';

import { AssertionError } from 'chai';

import { createArtifactWithRandomId } from './artifacts';

export enum HexColors {
  GREEN = '#00b140',
  PURPLE = '#6a0dad',
  RED = '#ff0000',
  BLUE = '#0000ff',
  WHITE = '#ffffff',
  TRANSPARENT = '#00000000'
}

function hexToRgba (
  hexColor: string
): [number, number, number, number] | undefined {
  const match = hexColor.match(/^#([0-9a-fA-F]{6,8})$/);
  if (!match) return;

  const colorStr = match[1];
  return [
    parseInt(colorStr.substring(0, 2), 16),
    parseInt(colorStr.substring(2, 4), 16),
    parseInt(colorStr.substring(4, 6), 16),
    parseInt(colorStr.substring(6, 8), 16) || 0xff
  ];
}

function formatHexByte (val: number): string {
  const str = val.toString(16);
  return str.length === 2 ? str : `0${str}`;
}

/**
 * Get the hex color at the given pixel coordinate in an image.
 */
export function getPixelColor (
  image: Electron.NativeImage,
  point: Electron.Point,
  includeAlpha: boolean = false
): string {
  // image.crop crashes if point is fractional, so round to prevent that crash
  const pixel = image.crop({
    x: Math.round(point.x),
    y: Math.round(point.y),
    width: 1,
    height: 1
  });
  // TODO(samuelmaddock): NativeImage.toBitmap() should return the raw pixel
  // color, but it sometimes differs. Why is that?
  const [b, g, r, a] = pixel.toBitmap();
  let hex = `#${formatHexByte(r)}${formatHexByte(g)}${formatHexByte(b)}`;
  if (includeAlpha) hex += `${formatHexByte(a)}`;
  return hex;
}

/** Calculate euclidean distance between colors. */
function colorDistance (hexColorA: string, hexColorB: string): number {
  const colorA = hexToRgba(hexColorA);
  const colorB = hexToRgba(hexColorB);
  if (!colorA || !colorB) return -1;
  return Math.sqrt(
    Math.pow(colorB[0] - colorA[0], 2) +
      Math.pow(colorB[1] - colorA[1], 2) +
      Math.pow(colorB[2] - colorA[2], 2)
  );
}

/**
 * Determine if colors are similar based on distance. This can be useful when
 * comparing colors which may differ based on lossy compression.
 */
export function areColorsSimilar (
  hexColorA: string,
  hexColorB: string,
  distanceThreshold = 90
): boolean {
  const distance = colorDistance(hexColorA, hexColorB);
  return distance <= distanceThreshold;
}

function displayCenter (display: Electron.Display): Electron.Point {
  return {
    x: display.size.width / 2,
    y: display.size.height / 2
  };
}

/** Resolve when approx. one frame has passed (30FPS) */
export async function nextFrameTime (): Promise<void> {
  return await new Promise((resolve) => {
    setTimeout(resolve, 1000 / 30);
  });
}

/**
 * Utilities for creating and inspecting a screen capture.
 *
 * Set `PAUSE_CAPTURE_TESTS` env var to briefly pause during screen
 * capture for easier inspection.
 *
 * NOTE: Not yet supported on Linux in CI due to empty sources list.
 */
export class ScreenCapture {
  /** Timeout to wait for expected color to match. */
  static TIMEOUT = 3000;

  constructor (display?: Electron.Display) {
    this.display = display || screen.getPrimaryDisplay();
  }

  public async expectColorAtCenterMatches (hexColor: string) {
    return this._expectImpl(displayCenter(this.display), hexColor, true);
  }

  public async expectColorAtCenterDoesNotMatch (hexColor: string) {
    return this._expectImpl(displayCenter(this.display), hexColor, false);
  }

  public async expectColorAtPointOnDisplayMatches (
    hexColor: string,
    findPoint: (displaySize: Electron.Size) => Electron.Point
  ) {
    return this._expectImpl(findPoint(this.display.size), hexColor, true);
  }

  public async takeScreenshot (filePrefix: string) {
    const frame = await this.captureFrame();
    return await createArtifactWithRandomId(
      (id) => `${filePrefix}-${id}.png`,
      frame.toPNG()
    );
  }

  private async captureFrame (): Promise<NativeImage> {
    const sources = await desktopCapturer.getSources({
      types: ['screen'],
      thumbnailSize: this.display.size
    });

    const captureSource = sources.find(
      (source) => source.display_id === this.display.id.toString()
    );
    if (captureSource === undefined) {
      const displayIds = sources.map((source) => source.display_id).join(', ');
      throw new Error(
        `Unable to find screen capture for display '${this.display.id}'\n\tAvailable displays: ${displayIds}`
      );
    }

    if (process.env.PAUSE_CAPTURE_TESTS) {
      await new Promise((resolve) => setTimeout(resolve, 1e3));
    }

    return captureSource.thumbnail;
  }

  private async _expectImpl (
    point: Electron.Point,
    expectedColor: string,
    matchIsExpected: boolean
  ) {
    let frame: Electron.NativeImage;
    let actualColor: string;
    let gotExpectedResult: boolean = false;
    const expiration = Date.now() + ScreenCapture.TIMEOUT;

    // Continuously capture frames until we either see the expected result or
    // reach a timeout. This helps avoid flaky tests in which a short waiting
    // period is required for the expected result.
    do {
      frame = await this.captureFrame();
      actualColor = getPixelColor(frame, point);
      const colorsMatch = areColorsSimilar(expectedColor, actualColor);
      gotExpectedResult = matchIsExpected ? colorsMatch : !colorsMatch;
      if (gotExpectedResult) break;

      await nextFrameTime(); // limit framerate
    } while (Date.now() < expiration);

    if (!gotExpectedResult) {
      // Limit image to 720p to save on storage space
      if (process.env.CI) {
        const width = Math.floor(Math.min(frame.getSize().width, 720));
        frame = frame.resize({ width });
      }

      // Save the image as an artifact for better debugging
      const artifactName = await createArtifactWithRandomId(
        (id) => `color-mismatch-${id}.png`,
        frame.toPNG()
      );

      throw new AssertionError(
        `Expected color at (${point.x}, ${point.y}) to ${
          matchIsExpected ? 'match' : '*not* match'
        } '${expectedColor}', but got '${actualColor}'. See the artifact '${artifactName}' for more information.`
      );
    }
  }

  private display: Electron.Display;
}

/**
 * Whether the current VM has a valid screen which can be used to capture.
 *
 * This is specific to Electron's CI test runners.
 * - Linux: virtual screen display is 0x0
 * - Win32 arm64 (WOA): virtual screen display is 0x0
 * - Win32 ia32: skipped
 * - Win32 x64: virtual screen display is 0x0
 */
export const hasCapturableScreen = () => {
  return process.env.CI ? process.platform === 'darwin' : true;
};
