import { screen, desktopCapturer, NativeImage } from 'electron';
import { createArtifactWithRandomId } from './artifacts';
import { AssertionError } from 'chai';

export enum HexColors {
  GREEN = '#00b140',
  PURPLE = '#6a0dad',
  RED = '#ff0000',
  BLUE = '#0000ff',
  WHITE = '#ffffff',
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
function getPixelColor (
  image: Electron.NativeImage,
  point: Electron.Point
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
  const [b, g, r] = pixel.toBitmap();
  return `#${formatHexByte(r)}${formatHexByte(g)}${formatHexByte(b)}`;
}

/** Calculate euclidian distance between colors. */
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
function areColorsSimilar (
  hexColorA: string,
  hexColorB: string,
  distanceThreshold = 90
): boolean {
  const distance = colorDistance(hexColorA, hexColorB);
  return distance <= distanceThreshold;
}

function imageCenter (image: NativeImage): Electron.Point {
  const size = image.getSize();
  return {
    x: size.width / 2,
    y: size.height / 2
  };
}
/**
 * Utilities for creating and inspecting a screen capture.
 *
 * NOTE: Not yet supported on Linux in CI due to empty sources list.
 */
export class ScreenCapture {
  /** Use the async constructor `ScreenCapture.create()` instead. */
  private constructor (image: NativeImage) {
    this.image = image;
  }

  public static async create (): Promise<ScreenCapture> {
    const display = screen.getPrimaryDisplay();
    return ScreenCapture._createImpl(display);
  }

  public static async createForDisplay (
    display: Electron.Display
  ): Promise<ScreenCapture> {
    return ScreenCapture._createImpl(display);
  }

  public async expectColorAtCenterMatches (hexColor: string) {
    return this._expectImpl(imageCenter(this.image), hexColor, true);
  }

  public async expectColorAtCenterDoesNotMatch (hexColor: string) {
    return this._expectImpl(imageCenter(this.image), hexColor, false);
  }

  public async expectColorAtPointOnDisplayMatches (
    hexColor: string,
    findPoint: (displaySize: Electron.Size) => Electron.Point
  ) {
    return this._expectImpl(findPoint(this.image.getSize()), hexColor, true);
  }

  private static async _createImpl (display: Electron.Display) {
    const sources = await desktopCapturer.getSources({
      types: ['screen'],
      thumbnailSize: display.size
    });

    const captureSource = sources.find(
      (source) => source.display_id === display.id.toString()
    );
    if (captureSource === undefined) {
      const displayIds = sources.map((source) => source.display_id).join(', ');
      throw new Error(
        `Unable to find screen capture for display '${display.id}'\n\tAvailable displays: ${displayIds}`
      );
    }

    return new ScreenCapture(captureSource.thumbnail);
  }

  private async _expectImpl (
    point: Electron.Point,
    expectedColor: string,
    matchIsExpected: boolean
  ) {
    const actualColor = getPixelColor(this.image, point);
    const colorsMatch = areColorsSimilar(expectedColor, actualColor);
    const gotExpectedResult = matchIsExpected ? colorsMatch : !colorsMatch;

    if (!gotExpectedResult) {
      // Save the image as an artifact for better debugging
      const artifactName = await createArtifactWithRandomId(
        (id) => `color-mismatch-${id}.png`,
        this.image.toPNG()
      );
      throw new AssertionError(
        `Expected color at (${point.x}, ${point.y}) to ${
          matchIsExpected ? 'match' : '*not* match'
        } '${expectedColor}', but got '${actualColor}'. See the artifact '${artifactName}' for more information.`
      );
    }
  }

  private image: NativeImage;
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
  return process.platform === 'darwin';
};
