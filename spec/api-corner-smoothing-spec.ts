import { NativeImage, nativeImage } from 'electron/common';
import { BrowserWindow } from 'electron/main';

import { AssertionError, expect } from 'chai';

import path = require('node:path');

import { createArtifact } from './lib/artifacts';
import { ifdescribe } from './lib/spec-helpers';
import { closeAllWindows } from './lib/window-helpers';

const FIXTURE_PATH = path.resolve(
  __dirname,
  'fixtures',
  'api',
  'corner-smoothing'
);

/**
 * Rendered images may "match" but slightly differ due to rendering artifacts
 * like anti-aliasing and vector path resolution, among others. This tolerance
 * is the cutoff for whether two images "match" or not.
 *
 * From testing, matching images were found to have an average global difference
 * up to about 1.3 and mismatched images were found to have a difference of at
 * least about 7.3.
 *
 * See the documentation on `compareImages` for more information.
 */
const COMPARISON_TOLERANCE = 2.5;

/**
 * Compares the average global difference of two images to determine if they
 * are similar enough to "match" each other.
 *
 * "Average global difference" is the average difference of pixel components
 * (RGB each) across an entire image.
 *
 * The cutoff for matching/not-matching is defined by the `COMPARISON_TOLERANCE`
 * constant.
 */
function compareImages (img1: NativeImage, img2: NativeImage): boolean {
  expect(img1.getSize()).to.deep.equal(
    img2.getSize(),
    'Cannot compare images with different sizes'
  );

  const bitmap1 = img1.toBitmap();
  const bitmap2 = img2.toBitmap();

  const { width, height } = img1.getSize();

  let totalDiff = 0;
  for (let x = 0; x < width; x++) {
    for (let y = 0; y < height; y++) {
      const index = (x + y * width) * 4;
      const pixel1 = bitmap1.subarray(index, index + 4);
      const pixel2 = bitmap2.subarray(index, index + 4);
      const diff =
        Math.abs(pixel1[0] - pixel2[0]) +
        Math.abs(pixel1[1] - pixel2[1]) +
        Math.abs(pixel1[2] - pixel2[2]);
      totalDiff += diff;
    }
  }

  const avgDiff = totalDiff / (width * height);
  return avgDiff <= COMPARISON_TOLERANCE;
}

/**
 * Recipe for tests.
 *
 * The page is rendered, captured as an image, then compared to an expected
 * result image.
 */
async function pageCaptureTestRecipe (
  pagePath: string,
  expectedImgPath: string,
  artifactName: string,
  cornerSmoothingAvailable: boolean = true
): Promise<void> {
  const w = new BrowserWindow({
    show: false,
    width: 800,
    height: 600,
    useContentSize: true,
    webPreferences: {
      enableCornerSmoothingCSS: cornerSmoothingAvailable
    }
  });
  await w.loadFile(pagePath);
  w.show();

  // Wait for a render frame to prepare the page.
  await w.webContents.executeJavaScript(
    'new Promise((resolve) => { requestAnimationFrame(() => resolve()); })'
  );

  const actualImg = await w.webContents.capturePage();
  expect(actualImg.isEmpty()).to.be.false('Failed to capture page image');

  const expectedImg = nativeImage.createFromPath(expectedImgPath);
  expect(expectedImg.isEmpty()).to.be.false(
    'Failed to read expected reference image'
  );

  const matches = compareImages(actualImg, expectedImg);
  if (!matches) {
    const artifactFileName = `corner-rounding-expected-${artifactName}.png`;
    await createArtifact(artifactFileName, actualImg.toPNG());

    throw new AssertionError(
      `Actual image did not match expected reference image. Actual: "${artifactFileName}" in artifacts, Expected: "${path.relative(
        path.resolve(__dirname, '..'),
        expectedImgPath
      )}" in source`
    );
  }
}

// FIXME: these tests rely on live rendering results, which are too variable to
// reproduce outside of CI, primarily due to display scaling.
ifdescribe(!!process.env.CI)('-electron-corner-smoothing', () => {
  afterEach(async () => {
    await closeAllWindows();
  });

  describe('shape', () => {
    for (const available of [true, false]) {
      it(`matches the reference with web preference = ${available}`, async () => {
        await pageCaptureTestRecipe(
          path.join(FIXTURE_PATH, 'shape', 'test.html'),
          path.join(FIXTURE_PATH, 'shape', `expected-${available}.png`),
          `shape-${available}`,
          available
        );
      });
    }
  });

  describe('system-ui keyword', () => {
    const { platform } = process;
    it(`matches the reference for platform = ${platform}`, async () => {
      await pageCaptureTestRecipe(
        path.join(FIXTURE_PATH, 'system-ui-keyword', 'test.html'),
        path.join(
          FIXTURE_PATH,
          'system-ui-keyword',
          `expected-${platform}.png`
        ),
        `system-ui-${platform}`
      );
    });
  });
});
