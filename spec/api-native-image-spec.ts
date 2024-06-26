import { expect } from 'chai';
import { nativeImage } from 'electron/common';
import { ifdescribe, ifit, itremote, useRemoteContext } from './lib/spec-helpers';
import * as path from 'node:path';

describe('nativeImage module', () => {
  const fixturesPath = path.join(__dirname, 'fixtures');

  const imageLogo = {
    path: path.join(fixturesPath, 'assets', 'logo.png'),
    width: 538,
    height: 190
  };
  const image1x1 = {
    dataUrl: 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAAXNSR0IArs4c6QAAAAtJREFUGJVjYAACAAAFAAHWnxbNAAAAAElFTkSuQmCC',
    path: path.join(fixturesPath, 'assets', '1x1.png'),
    height: 1,
    width: 1
  };
  const image2x2 = {
    dataUrl: 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAIAAAD91JpzAAAAAXNSR0IArs4c6QAAABZJREFUGJVj/P//PwMDAxMDAwMDAwMAJAYDAQTM5XcAAAAASUVORK5CYII=',
    path: path.join(fixturesPath, 'assets', '2x2.jpg'),
    height: 2,
    width: 2
  };
  const image3x3 = {
    dataUrl: 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAMAAAADCAYAAABWKLW/AAABB2lDQ1BTa2lhAAAokX2QP0vDQByGn1NBBEEHBweHG1wttQXp4FSF4BojpN0uMUYh/7ikdHURZ0GcRfwM9YOILi79CA7irJeA1yW+08PLy93DD8QcYKULaVZp1xlKfzSWq3MEgjoqLAvaI+D7vdm+7v2za8vaeVSGwCfgaX80BnEMbMUN+4aDhhPD06qofr+9Nqw99wjEPbAZL3CwwGGhzf4ZOEyTSWi9WY+ys1NgAOxQ4uIwROKgSJlQIZlyRcUlkh4dekg8NIqMkgsijLJsnswfYfAFy3e2Cx7g5Ra2P2y3+wQbNzB7s529YaG0+rvOUr/f4ixrZ4ecnJiECMkJGSGd2rXLPgc/b9ZFPoj6CWEAAAAMSURBVBiVY2AgCgAAACcAAZlYQycAAAAASUVORK5CYII=',
    path: path.join(fixturesPath, 'assets', '3x3.png'),
    height: 3,
    width: 3
  };

  const dataUrlImages = [
    image1x1,
    image2x2,
    image3x3
  ];

  ifdescribe(process.platform === 'darwin')('isMacTemplateImage state', () => {
    describe('with properties', () => {
      it('correctly recognizes a template image', () => {
        const image = nativeImage.createFromPath(path.join(fixturesPath, 'assets', 'logo.png'));
        expect(image.isMacTemplateImage).to.be.false();

        const templateImage = nativeImage.createFromPath(path.join(fixturesPath, 'assets', 'logo_Template.png'));
        expect(templateImage.isMacTemplateImage).to.be.true();
      });

      it('sets a template image', function () {
        const image = nativeImage.createFromPath(path.join(fixturesPath, 'assets', 'logo.png'));
        expect(image.isMacTemplateImage).to.be.false();

        image.isMacTemplateImage = true;
        expect(image.isMacTemplateImage).to.be.true();
      });
    });

    describe('with functions', () => {
      it('correctly recognizes a template image', () => {
        const image = nativeImage.createFromPath(path.join(fixturesPath, 'assets', 'logo.png'));
        expect(image.isTemplateImage()).to.be.false();

        const templateImage = nativeImage.createFromPath(path.join(fixturesPath, 'assets', 'logo_Template.png'));
        expect(templateImage.isTemplateImage()).to.be.true();
      });

      it('sets a template image', function () {
        const image = nativeImage.createFromPath(path.join(fixturesPath, 'assets', 'logo.png'));
        expect(image.isTemplateImage()).to.be.false();

        image.setTemplateImage(true);
        expect(image.isTemplateImage()).to.be.true();
      });
    });
  });

  describe('createEmpty()', () => {
    it('returns an empty image', () => {
      const empty = nativeImage.createEmpty();
      expect(empty.isEmpty()).to.be.true();
      expect(empty.getAspectRatio()).to.equal(1);
      expect(empty.toDataURL()).to.equal('data:image/png;base64,');
      expect(empty.toDataURL({ scaleFactor: 2.0 })).to.equal('data:image/png;base64,');
      expect(empty.getSize()).to.deep.equal({ width: 0, height: 0 });
      expect(empty.getBitmap()).to.be.empty();
      expect(empty.getBitmap({ scaleFactor: 2.0 })).to.be.empty();
      expect(empty.toBitmap()).to.be.empty();
      expect(empty.toBitmap({ scaleFactor: 2.0 })).to.be.empty();
      expect(empty.toJPEG(100)).to.be.empty();
      expect(empty.toPNG()).to.be.empty();
      expect(empty.toPNG({ scaleFactor: 2.0 })).to.be.empty();

      if (process.platform === 'darwin') {
        expect(empty.getNativeHandle()).to.be.empty();
      }
    });
  });

  describe('createFromBitmap(buffer, options)', () => {
    it('returns an empty image when the buffer is empty', () => {
      expect(nativeImage.createFromBitmap(Buffer.from([]), { width: 0, height: 0 }).isEmpty()).to.be.true();
    });

    it('returns an image created from the given buffer', () => {
      const imageA = nativeImage.createFromPath(path.join(fixturesPath, 'assets', 'logo.png'));

      const imageB = nativeImage.createFromBitmap(imageA.toBitmap(), imageA.getSize());
      expect(imageB.getSize()).to.deep.equal({ width: 538, height: 190 });

      const imageC = nativeImage.createFromBuffer(imageA.toBitmap(), { ...imageA.getSize(), scaleFactor: 2.0 });
      expect(imageC.getSize()).to.deep.equal({ width: 269, height: 95 });
    });

    it('throws on invalid arguments', () => {
      expect(() => nativeImage.createFromBitmap(null as any, {} as any)).to.throw('buffer must be a node Buffer');
      expect(() => nativeImage.createFromBitmap([12, 14, 124, 12] as any, {} as any)).to.throw('buffer must be a node Buffer');
      expect(() => nativeImage.createFromBitmap(Buffer.from([]), {} as any)).to.throw('width is required');
      expect(() => nativeImage.createFromBitmap(Buffer.from([]), { width: 1 } as any)).to.throw('height is required');
      expect(() => nativeImage.createFromBitmap(Buffer.from([]), { width: 1, height: 1 })).to.throw('invalid buffer size');
    });
  });

  describe('createFromBuffer(buffer, options)', () => {
    it('returns an empty image when the buffer is empty', () => {
      expect(nativeImage.createFromBuffer(Buffer.from([])).isEmpty()).to.be.true();
    });

    it('returns an empty image when the buffer is too small', () => {
      const image = nativeImage.createFromBuffer(Buffer.from([1, 2, 3, 4]), { width: 100, height: 100 });
      expect(image.isEmpty()).to.be.true();
    });

    it('returns an image created from the given buffer', () => {
      const imageA = nativeImage.createFromPath(path.join(fixturesPath, 'assets', 'logo.png'));

      const imageB = nativeImage.createFromBuffer(imageA.toPNG());
      expect(imageB.getSize()).to.deep.equal({ width: 538, height: 190 });
      expect(imageA.toBitmap().equals(imageB.toBitmap())).to.be.true();

      const imageC = nativeImage.createFromBuffer(imageA.toJPEG(100));
      expect(imageC.getSize()).to.deep.equal({ width: 538, height: 190 });

      const imageD = nativeImage.createFromBuffer(imageA.toBitmap(),
        { width: 538, height: 190 });
      expect(imageD.getSize()).to.deep.equal({ width: 538, height: 190 });

      const imageE = nativeImage.createFromBuffer(imageA.toBitmap(),
        { width: 100, height: 200 });
      expect(imageE.getSize()).to.deep.equal({ width: 100, height: 200 });

      const imageF = nativeImage.createFromBuffer(imageA.toBitmap());
      expect(imageF.isEmpty()).to.be.true();

      const imageG = nativeImage.createFromBuffer(imageA.toPNG(),
        { width: 100, height: 200 });
      expect(imageG.getSize()).to.deep.equal({ width: 538, height: 190 });

      const imageH = nativeImage.createFromBuffer(imageA.toJPEG(100),
        { width: 100, height: 200 });
      expect(imageH.getSize()).to.deep.equal({ width: 538, height: 190 });

      const imageI = nativeImage.createFromBuffer(imageA.toBitmap(),
        { width: 538, height: 190, scaleFactor: 2.0 });
      expect(imageI.getSize()).to.deep.equal({ width: 269, height: 95 });
    });

    it('throws on invalid arguments', () => {
      expect(() => nativeImage.createFromBuffer(null as any)).to.throw('buffer must be a node Buffer');
      expect(() => nativeImage.createFromBuffer([12, 14, 124, 12] as any)).to.throw('buffer must be a node Buffer');
    });
  });

  describe('createFromDataURL(dataURL)', () => {
    it('returns an empty image from the empty string', () => {
      expect(nativeImage.createFromDataURL('').isEmpty()).to.be.true();
    });

    it('returns an image created from the given string', () => {
      for (const imageData of dataUrlImages) {
        const imageFromPath = nativeImage.createFromBuffer(
          nativeImage.createFromPath(imageData.path).toPNG());
        const imageFromDataUrl = nativeImage.createFromDataURL(imageData.dataUrl!);

        expect(imageFromDataUrl.isEmpty()).to.be.false();
        expect(imageFromDataUrl.getSize()).to.deep.equal(imageFromPath.getSize());
        expect(imageFromDataUrl.toBitmap()).to.satisfy(
          (bitmap: any) => imageFromPath.toBitmap().equals(bitmap));
        expect(imageFromDataUrl.toDataURL()).to.equal(imageFromPath.toDataURL());
      }
    });
  });

  describe('toDataURL()', () => {
    it('returns a PNG data URL', () => {
      for (const imageData of dataUrlImages) {
        const imageFromPath = nativeImage.createFromBuffer(
          nativeImage.createFromPath(imageData.path!).toPNG());

        const scaleFactors = [1.0, 2.0];
        for (const scaleFactor of scaleFactors) {
          expect(imageFromPath.toDataURL({ scaleFactor })).to.equal(imageData.dataUrl);
        }
      }
    });

    it('returns a data URL at 1x scale factor by default', () => {
      const imageData = imageLogo;
      const image = nativeImage.createFromPath(imageData.path);

      const imageOne = nativeImage.createFromBuffer(image.toPNG(), {
        width: image.getSize().width,
        height: image.getSize().height,
        scaleFactor: 2.0
      });
      expect(imageOne.getSize()).to.deep.equal(
        { width: imageData.width / 2, height: imageData.height / 2 });

      const imageTwo = nativeImage.createFromDataURL(imageOne.toDataURL());
      expect(imageTwo.getSize()).to.deep.equal(
        { width: imageData.width, height: imageData.height });

      expect(imageOne.toBitmap().equals(imageTwo.toBitmap())).to.be.true();
    });

    it('supports a scale factor', () => {
      const imageData = imageLogo;
      const image = nativeImage.createFromPath(imageData.path);
      const expectedSize = { width: imageData.width, height: imageData.height };

      const imageFromDataUrlOne = nativeImage.createFromDataURL(
        image.toDataURL({ scaleFactor: 1.0 }));
      expect(imageFromDataUrlOne.getSize()).to.deep.equal(expectedSize);

      const imageFromDataUrlTwo = nativeImage.createFromDataURL(
        image.toDataURL({ scaleFactor: 2.0 }));
      expect(imageFromDataUrlTwo.getSize()).to.deep.equal(expectedSize);
    });
  });

  describe('toPNG()', () => {
    it('returns a buffer at 1x scale factor by default', () => {
      const imageData = imageLogo;
      const imageA = nativeImage.createFromPath(imageData.path);

      const imageB = nativeImage.createFromBuffer(imageA.toPNG(), {
        width: imageA.getSize().width,
        height: imageA.getSize().height,
        scaleFactor: 2.0
      });
      expect(imageB.getSize()).to.deep.equal(
        { width: imageData.width / 2, height: imageData.height / 2 });

      const imageC = nativeImage.createFromBuffer(imageB.toPNG());
      expect(imageC.getSize()).to.deep.equal(
        { width: imageData.width, height: imageData.height });

      expect(imageB.toBitmap().equals(imageC.toBitmap())).to.be.true();
    });

    it('supports a scale factor', () => {
      const imageData = imageLogo;
      const image = nativeImage.createFromPath(imageData.path);

      const imageFromBufferOne = nativeImage.createFromBuffer(
        image.toPNG({ scaleFactor: 1.0 }));
      expect(imageFromBufferOne.getSize()).to.deep.equal(
        { width: imageData.width, height: imageData.height });

      const imageFromBufferTwo = nativeImage.createFromBuffer(
        image.toPNG({ scaleFactor: 2.0 }), { scaleFactor: 2.0 });
      expect(imageFromBufferTwo.getSize()).to.deep.equal(
        { width: imageData.width / 2, height: imageData.height / 2 });
    });
  });

  describe('createFromPath(path)', () => {
    it('returns an empty image for invalid paths', () => {
      expect(nativeImage.createFromPath('').isEmpty()).to.be.true();
      expect(nativeImage.createFromPath('does-not-exist.png').isEmpty()).to.be.true();
      expect(nativeImage.createFromPath('does-not-exist.ico').isEmpty()).to.be.true();
      expect(nativeImage.createFromPath(__dirname).isEmpty()).to.be.true();
      expect(nativeImage.createFromPath(__filename).isEmpty()).to.be.true();
    });

    it('loads images from paths relative to the current working directory', () => {
      const imagePath = path.relative('.', path.join(fixturesPath, 'assets', 'logo.png'));
      const image = nativeImage.createFromPath(imagePath);
      expect(image.isEmpty()).to.be.false();
      expect(image.getSize()).to.deep.equal({ width: 538, height: 190 });
    });

    it('loads images from paths with `.` segments', () => {
      const imagePath = `${path.join(fixturesPath)}${path.sep}.${path.sep}${path.join('assets', 'logo.png')}`;
      const image = nativeImage.createFromPath(imagePath);
      expect(image.isEmpty()).to.be.false();
      expect(image.getSize()).to.deep.equal({ width: 538, height: 190 });
    });

    it('loads images from paths with `..` segments', () => {
      const imagePath = `${path.join(fixturesPath, 'api')}${path.sep}..${path.sep}${path.join('assets', 'logo.png')}`;
      const image = nativeImage.createFromPath(imagePath);
      expect(image.isEmpty()).to.be.false();
      expect(image.getSize()).to.deep.equal({ width: 538, height: 190 });
    });

    ifit(process.platform === 'darwin')('Gets an NSImage pointer on macOS', function () {
      const imagePath = `${path.join(fixturesPath, 'api')}${path.sep}..${path.sep}${path.join('assets', 'logo.png')}`;
      const image = nativeImage.createFromPath(imagePath);
      const nsimage = image.getNativeHandle();

      expect(nsimage).to.have.lengthOf(8);

      // If all bytes are null, that's Bad
      const allBytesAreNotNull = nsimage.reduce((acc, x) => acc || (x !== 0), false);
      expect(allBytesAreNotNull);
    });

    ifit(process.platform === 'win32')('loads images from .ico files on Windows', function () {
      const imagePath = path.join(fixturesPath, 'assets', 'icon.ico');
      const image = nativeImage.createFromPath(imagePath);
      expect(image.isEmpty()).to.be.false();
      expect(image.getSize()).to.deep.equal({ width: 256, height: 256 });
    });
  });

  describe('createFromNamedImage(name)', () => {
    it('returns empty for invalid options', () => {
      const image = nativeImage.createFromNamedImage('totally_not_real');
      expect(image.isEmpty()).to.be.true();
    });

    ifit(process.platform !== 'darwin')('returns empty on non-darwin platforms', function () {
      const image = nativeImage.createFromNamedImage('NSActionTemplate');
      expect(image.isEmpty()).to.be.true();
    });

    ifit(process.platform === 'darwin')('returns a valid image on darwin', function () {
      const image = nativeImage.createFromNamedImage('NSActionTemplate');
      expect(image.isEmpty()).to.be.false();
    });

    ifit(process.platform === 'darwin')('returns allows an HSL shift for a valid image on darwin', function () {
      const image = nativeImage.createFromNamedImage('NSActionTemplate', [0.5, 0.2, 0.8]);
      expect(image.isEmpty()).to.be.false();
    });
  });

  describe('resize(options)', () => {
    it('returns a resized image', () => {
      const image = nativeImage.createFromPath(path.join(fixturesPath, 'assets', 'logo.png'));
      for (const [resizeTo, expectedSize] of new Map([
        [{}, { width: 538, height: 190 }],
        [{ width: 269 }, { width: 269, height: 95 }],
        [{ width: 600 }, { width: 600, height: 212 }],
        [{ height: 95 }, { width: 269, height: 95 }],
        [{ height: 200 }, { width: 566, height: 200 }],
        [{ width: 80, height: 65 }, { width: 80, height: 65 }],
        [{ width: 600, height: 200 }, { width: 600, height: 200 }],
        [{ width: 0, height: 0 }, { width: 0, height: 0 }],
        [{ width: -1, height: -1 }, { width: 0, height: 0 }]
      ])) {
        const actualSize = image.resize(resizeTo).getSize();
        expect(actualSize).to.deep.equal(expectedSize);
      }
    });

    it('returns an empty image when called on an empty image', () => {
      expect(nativeImage.createEmpty().resize({ width: 1, height: 1 }).isEmpty()).to.be.true();
      expect(nativeImage.createEmpty().resize({ width: 0, height: 0 }).isEmpty()).to.be.true();
    });

    it('supports a quality option', () => {
      const image = nativeImage.createFromPath(path.join(fixturesPath, 'assets', 'logo.png'));
      const good = image.resize({ width: 100, height: 100, quality: 'good' });
      const better = image.resize({ width: 100, height: 100, quality: 'better' });
      const best = image.resize({ width: 100, height: 100, quality: 'best' });

      expect(good.toPNG()).to.have.lengthOf.at.most(better.toPNG().length);
      expect(better.toPNG()).to.have.lengthOf.below(best.toPNG().length);
    });
  });

  describe('crop(bounds)', () => {
    it('returns an empty image when called on an empty image', () => {
      expect(nativeImage.createEmpty().crop({ width: 1, height: 2, x: 0, y: 0 }).isEmpty()).to.be.true();
      expect(nativeImage.createEmpty().crop({ width: 0, height: 0, x: 0, y: 0 }).isEmpty()).to.be.true();
    });

    it('returns an empty image when the bounds are invalid', () => {
      const image = nativeImage.createFromPath(path.join(fixturesPath, 'assets', 'logo.png'));
      expect(image.crop({ width: 0, height: 0, x: 0, y: 0 }).isEmpty()).to.be.true();
      expect(image.crop({ width: -1, height: 10, x: 0, y: 0 }).isEmpty()).to.be.true();
      expect(image.crop({ width: 10, height: -35, x: 0, y: 0 }).isEmpty()).to.be.true();
      expect(image.crop({ width: 100, height: 100, x: 1000, y: 1000 }).isEmpty()).to.be.true();
    });

    it('returns a cropped image', () => {
      const image = nativeImage.createFromPath(path.join(fixturesPath, 'assets', 'logo.png'));
      const cropA = image.crop({ width: 25, height: 64, x: 0, y: 0 });
      const cropB = image.crop({ width: 25, height: 64, x: 30, y: 40 });
      expect(cropA.getSize()).to.deep.equal({ width: 25, height: 64 });
      expect(cropB.getSize()).to.deep.equal({ width: 25, height: 64 });
      expect(cropA.toPNG().equals(cropB.toPNG())).to.be.false();
    });

    it('toBitmap() returns a buffer of the right size', () => {
      const image = nativeImage.createFromPath(path.join(fixturesPath, 'assets', 'logo.png'));
      const crop = image.crop({ width: 25, height: 64, x: 0, y: 0 });
      expect(crop.toBitmap().length).to.equal(25 * 64 * 4);
    });
  });

  describe('getAspectRatio()', () => {
    it('returns an aspect ratio of an empty image', () => {
      expect(nativeImage.createEmpty().getAspectRatio()).to.equal(1.0);
    });

    it('returns an aspect ratio of an image', () => {
      const imageData = imageLogo;
      // imageData.width / imageData.height = 2.831578947368421
      const expectedAspectRatio = 2.8315789699554443;

      const image = nativeImage.createFromPath(imageData.path);
      expect(image.getAspectRatio()).to.equal(expectedAspectRatio);
    });
  });

  ifdescribe(process.platform !== 'linux')('createThumbnailFromPath(path, size)', () => {
    useRemoteContext({ webPreferences: { contextIsolation: false, nodeIntegration: true } });

    it('throws when invalid size is passed', async () => {
      const badSize = { width: -1, height: -1 };

      await expect(
        nativeImage.createThumbnailFromPath('path', badSize)
      ).to.eventually.be.rejectedWith('size must not be empty');
    });

    it('throws when a bad path is passed', async () => {
      const badPath = process.platform === 'win32' ? '\\hey\\hi\\hello' : '/hey/hi/hello';
      const goodSize = { width: 100, height: 100 };

      await expect(
        nativeImage.createThumbnailFromPath(badPath, goodSize)
      ).to.eventually.be.rejected();
    });

    it('returns native image given valid params', async () => {
      const goodPath = path.join(fixturesPath, 'assets', 'logo.png');
      const goodSize = { width: 100, height: 100 };
      const result = await nativeImage.createThumbnailFromPath(goodPath, goodSize);
      expect(result.isEmpty()).to.equal(false);
    });

    it('returns the correct size if larger than the initial image', async () => {
      // capybara.png is a 128x128 image.
      const imgPath = path.join(fixturesPath, 'assets', 'capybara.png');
      const size = { width: 256, height: 256 };
      const result = await nativeImage.createThumbnailFromPath(imgPath, size);
      expect(result.getSize()).to.deep.equal(size);
    });

    it('returns the correct size if is the same as the initial image', async () => {
      // capybara.png is a 128x128 image.
      const imgPath = path.join(fixturesPath, 'assets', 'capybara.png');
      const size = { width: 128, height: 128 };
      const result = await nativeImage.createThumbnailFromPath(imgPath, size);
      expect(result.getSize()).to.deep.equal(size);
    });

    it('returns the correct size if smaller than the initial image', async () => {
      // capybara.png is a 128x128 image.
      const imgPath = path.join(fixturesPath, 'assets', 'capybara.png');
      const maxSize = { width: 64, height: 64 };
      const result = await nativeImage.createThumbnailFromPath(imgPath, maxSize);
      expect(result.getSize()).to.deep.equal(maxSize);
    });

    itremote('works in the renderer', async (path: string) => {
      const { nativeImage } = require('electron');
      const goodSize = { width: 100, height: 100 };
      const result = await nativeImage.createThumbnailFromPath(path, goodSize);
      expect(result.isEmpty()).to.equal(false);
    }, [path.join(fixturesPath, 'assets', 'logo.png')]);
  });

  describe('addRepresentation()', () => {
    it('does not add representation when the buffer is too small', () => {
      const image = nativeImage.createEmpty();

      image.addRepresentation({
        buffer: Buffer.from([1, 2, 3, 4]),
        width: 100,
        height: 100
      });

      expect(image.isEmpty()).to.be.true();
    });

    it('supports adding a buffer representation for a scale factor', () => {
      const image = nativeImage.createEmpty();

      const imageDataOne = image1x1;
      image.addRepresentation({
        scaleFactor: 1.0,
        buffer: nativeImage.createFromPath(imageDataOne.path).toPNG()
      });

      expect(image.getScaleFactors()).to.deep.equal([1]);

      const imageDataTwo = image2x2;
      image.addRepresentation({
        scaleFactor: 2.0,
        buffer: nativeImage.createFromPath(imageDataTwo.path).toPNG()
      });

      expect(image.getScaleFactors()).to.deep.equal([1, 2]);

      const imageDataThree = image3x3;
      image.addRepresentation({
        scaleFactor: 3.0,
        buffer: nativeImage.createFromPath(imageDataThree.path).toPNG()
      });

      expect(image.getScaleFactors()).to.deep.equal([1, 2, 3]);

      image.addRepresentation({
        scaleFactor: 4.0,
        buffer: 'invalid' as any
      });

      // this one failed, so it shouldn't show up in the scale factors
      expect(image.getScaleFactors()).to.deep.equal([1, 2, 3]);

      expect(image.isEmpty()).to.be.false();
      expect(image.getSize()).to.deep.equal({ width: 1, height: 1 });

      expect(image.toDataURL({ scaleFactor: 1.0 })).to.equal(imageDataOne.dataUrl);
      expect(image.toDataURL({ scaleFactor: 2.0 })).to.equal(imageDataTwo.dataUrl);
      expect(image.toDataURL({ scaleFactor: 3.0 })).to.equal(imageDataThree.dataUrl);
      expect(image.toDataURL({ scaleFactor: 4.0 })).to.equal(imageDataThree.dataUrl);
    });

    it('supports adding a data URL representation for a scale factor', () => {
      const image = nativeImage.createEmpty();

      const imageDataOne = image1x1;
      image.addRepresentation({
        scaleFactor: 1.0,
        dataURL: imageDataOne.dataUrl
      });

      const imageDataTwo = image2x2;
      image.addRepresentation({
        scaleFactor: 2.0,
        dataURL: imageDataTwo.dataUrl
      });

      const imageDataThree = image3x3;
      image.addRepresentation({
        scaleFactor: 3.0,
        dataURL: imageDataThree.dataUrl
      });

      image.addRepresentation({
        scaleFactor: 4.0,
        dataURL: 'invalid'
      });

      expect(image.isEmpty()).to.be.false();
      expect(image.getSize()).to.deep.equal({ width: 1, height: 1 });

      expect(image.toDataURL({ scaleFactor: 1.0 })).to.equal(imageDataOne.dataUrl);
      expect(image.toDataURL({ scaleFactor: 2.0 })).to.equal(imageDataTwo.dataUrl);
      expect(image.toDataURL({ scaleFactor: 3.0 })).to.equal(imageDataThree.dataUrl);
      expect(image.toDataURL({ scaleFactor: 4.0 })).to.equal(imageDataThree.dataUrl);
    });

    it('supports adding a representation to an existing image', () => {
      const imageDataOne = image1x1;
      const image = nativeImage.createFromPath(imageDataOne.path);

      const imageDataTwo = image2x2;
      image.addRepresentation({
        scaleFactor: 2.0,
        dataURL: imageDataTwo.dataUrl
      });

      const imageDataThree = image3x3;
      image.addRepresentation({
        scaleFactor: 2.0,
        dataURL: imageDataThree.dataUrl
      });

      expect(image.toDataURL({ scaleFactor: 1.0 })).to.equal(imageDataOne.dataUrl);
      expect(image.toDataURL({ scaleFactor: 2.0 })).to.equal(imageDataTwo.dataUrl);
    });
  });
});
