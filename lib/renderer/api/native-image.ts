import { ipcRendererInternal } from '@electron/internal/renderer/ipc-renderer-internal';
import { IPC_MESSAGES } from '@electron/internal/common/ipc-messages';

const { nativeImage } = process._linkedBinding('electron_common_native_image');

nativeImage.createThumbnailFromPath = async (path: string, size: Electron.Size) => {
  return await ipcRendererInternal.invoke(IPC_MESSAGES.NATIVE_IMAGE_CREATE_THUMBNAIL_FROM_PATH, path, size);
};

export default nativeImage;
