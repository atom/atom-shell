import path = require('node:path');
import fs = require('node:fs/promises');
import { randomBytes } from 'node:crypto';

const IS_CI = !!process.env.CI;
const ARTIFACT_DIR = path.join(__dirname, '..', 'artifacts');

async function ensureArtifactDir (): Promise<void> {
  if (!IS_CI) {
    return;
  }

  await fs.mkdir(ARTIFACT_DIR, { recursive: true });
}

export async function createArtifact (
  fileName: string,
  data: Buffer
): Promise<void> {
  if (!IS_CI) {
    return;
  }

  await ensureArtifactDir();
  await fs.writeFile(path.join(ARTIFACT_DIR, fileName), data);
}

export async function createArtifactWithRandomId (
  makeFileName: (id: string) => string,
  data: Buffer
): Promise<string> {
  const randomId = randomBytes(12).toString('hex');
  const fileName = makeFileName(randomId);
  await createArtifact(fileName, data);
  return fileName;
}
