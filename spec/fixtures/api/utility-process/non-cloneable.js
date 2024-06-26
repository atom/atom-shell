const nonClonableObject = () => {};

process.parentPort.on('message', () => {
  try {
    process.parentPort.postMessage(nonClonableObject);
  } catch (error) {
    if (/An object could not be cloned/.test(error.message)) {
      process.parentPort.postMessage('caught-non-cloneable');
    }
  }
});
