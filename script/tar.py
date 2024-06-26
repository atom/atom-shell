import os
import sys
import tarfile

source = sys.argv[1]
target = sys.argv[2]

os.chdir(os.path.dirname(source))

with tarfile.open(name=os.path.basename(target), mode='w:gz') as tarball:
    tarball.add(os.path.relpath(source))
    tarball.close()
