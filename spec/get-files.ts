import * as fs from 'node:fs';
import * as path from 'node:path';

export async function getFiles (
  dir: string,
  test: ((file: string) => boolean) = (_: string) => true // eslint-disable-line @typescript-eslint/no-unused-vars
): Promise<string[]> {
  return fs.promises.readdir(dir)
    .then(files => files.map(file => path.join(dir, file)))
    .then(files => files.filter(file => test(file)));
}
