# Native Code and Electron

One of Electron's most powerful features is the ability to combine web technologies with native code - both for compute-intensive logic as well as for the occasional native user interface, where desired.

Electron does so by building on top of "Native Node.js Addons". You've probably already come across a few of them - packages like the famous [sqlite](https://www.npmjs.com/package/sqlite3) use native code to combine JavaScript and native technologies. You can use this feature to extend your Electron application with anything a fully native application can do:

* Access native platform APIs not available in JavaScript. Any macOS, Windows, or Linux operating system API is available to you.
* Create UI components that interact with native desktop frameworks.
* Integrate with existing native libraries.
* Implement performance-critical code that runs faster than JavaScript.

Native Node.js addons are dynamically-linked shared objects (on Unix-like systems) or DLL files (on Windows) that can be loaded into Node.js or Electron using the `require()` or `import` functions. They behave just like regular JavaScript modules but provide an interface to code written in C++, Rust, or other languages that can compile to native code.

# Tutorial: Creating a Native Node.js Addon for Electron

This tutorial will walk you through building a basic Node.js native addon that can be used in Electron applications. We'll focus on concepts common to all platforms, using C++ as the implementation language. Once you complete this tutorial common to all native Node.js addons, you can move on to one of our platform-specific tutorials.

## Requirements

This tutorial assumes you have Node.js and npm installed, as well as the basic tools necessary for compiling code on your platform (like Visual Studio on Windows, Xcode on macOS, or GCC/Clang on Linux). You can find detailed instructions in the [`node-gyp` readme](https://github.com/nodejs/node-gyp?tab=readme-ov-file).

### Requirements: macOS

To build native Node.js addons on macOS, you'll need the Xcode Command Line Tools. These provide the necessary compilers and build tools (namely, `clang`, `clang++`, and `make`). The following command will prompt you to install the Command Line Tools if they aren't already installed.

```sh
xcode-select --install
```

### Requirements: Windows

The official Node.js installer offers the optional installation of "Tools for Native Modules", which installs everything required for the basic compilation of C++ modules - specifically, Python 3 and the "Visual Studio Desktop development with C++" workload. Alternatively, you can use `chocolatey`, `winget`, or the Windows Store.

### Requirements: Linux

* [A supported version of Python](https://devguide.python.org/versions/)
* `make`
* A proper C/C++ compiler toolchain, like [GCC](https://gcc.gnu.org)

## 1) Creating a package

First, create a new Node.js package that will contain your native addon:

```sh
mkdir my-native-addon
cd my-native-addon
npm init -y
```

This creates a basic `package.json` file. Next, we'll install the necessary dependencies:

```sh
npm install node-addon-api bindings
```

* `node-addon-api`: This is a C++ wrapper for the low-level Node.js API that makes it easier to build addons. It provides a C++ object-oriented API that's more convenient and safer to use than the raw C-style API.
* `bindings`: A helper module that simplifies the process of loading your compiled native addon. It handles finding your compiled `.node` file automatically.

Now, let's update our `package.json` to include the appropriate build scripts. We will explain what these specifically do further below.

```json title='package.json'
{
  "name": "my-native-addon",
  "version": "1.0.0",
  "description": "A native addon for Electron",
  "main": "js/index.js",
  "scripts": {
    "clean": "node -e \"require('fs').rmSync('build', { recursive: true, force: true })\"",
    "build": "node-gyp configure && node-gyp build"
  },
  "dependencies": {
    "bindings": "^1.5.0",
    "node-addon-api": "^8.3.0"
  },
  "devDependencies": {
    "node-gyp": "^11.1.0"
  }
}
```

These scripts will:

* `clean`: Remove the build directory, allowing for a fresh build
* `build`: Run the standard node-gyp build process to compile your addon

## 2) Setting up the build system

Node.js addons use a build system called `node-gyp`, which is a cross-platform command-line tool written in Node.js. It compiles native addon modules for Node.js using platform-specific build tools behind the scenes:

* On Windows: Visual Studio
* On macOS: Xcode or command-line tools
* On Linux: GCC or similar compilers

### Configuring `node-gyp`

The `binding.gyp` file is a JSON-like configuration file that tells node-gyp how to build your native addon. It's similar to a make file or a project file but in a platform-independent format. Let's create a basic `binding.gyp` file:

```json title='binding.gyp'
{
  "targets": [
    {
      "target_name": "my_addon",
      "sources": [
        "src/my_addon.cc",
        "src/cpp_code.cc"
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "include"
      ],
      "dependencies": [
        "<!(node -p \"require('node-addon-api').gyp\")"
      ],
      "defines": [
        "NODE_ADDON_API_CPP_EXCEPTIONS"
      ],
      "cflags!": ["-fno-exceptions"],
      "cflags_cc!": ["-fno-exceptions"],
      "xcode_settings": {
        "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
        "CLANG_CXX_LIBRARY": "libc++",
        "MACOSX_DEPLOYMENT_TARGET": "10.14"
      },
      "msvs_settings": {
        "VCCLCompilerTool": {
          "ExceptionHandling": 1
        }
      }
    }
  ]
}
```

Let's break down this configuration:

* `target_name`: The name of your addon. This determines the filename of the compiled module (my_addon.node).
* `sources`: List of source files to compile. We'll have two files: the main addon file and our actual C++ implementation.
* `include_dirs`: Directories to search for header files. The cryptic-looking line `<!@(node -p \"require('node-addon-api').include\")` runs a Node.js command to get the path to the node-addon-api include directory.
* `dependencies`: The `node-addon-api` dependency. Similar to the include dirs, this executes a Node.js command to get the proper configuration.
* `defines`: Preprocessor definitions. Here we're enabling C++ exceptions for node-addon-api.
Platform-specific settings:
* `cflags`! and cflags_cc!: Compiler flags for Unix-like systems
* `xcode_settings`: Settings specific to macOS/Xcode compiler
* `msvs_settings`: Settings specific to Microsoft Visual Studio on Windows

Now, create the directory structure for our project:

```sh
mkdir src
mkdir include
mkdir js
```

This creates:

* `src/`: Where our source files will go
* `include/`: For header files
* `js/`: For our JavaScript wrapper

## 3) "Hello World" from C++

Let's start by defining our C++ interface in a header file. Create `include/cpp_code.h`:

```cpp
#pragma once
#include <string>

namespace cpp_code {
    // A simple function that takes a string input and returns a string
    std::string hello_world(const std::string& input);
} // namespace cpp_code
```

The `#pragma once` directive is a header guard that prevents the file from being included multiple times in the same compilation unit. The actual function declaration is inside a namespace to avoid potential name conflicts.

Next, let's implement the function in `src/cpp_code.cc`:

```cpp title='src/cpp_code.cc'
#include <string>
#include "../include/cpp_code.h"

namespace cpp_code {
    std::string hello_world(const std::string& input) {
        // Simply concatenate strings and return
        return "Hello from C++! You said: " + input;
    }
} // namespace cpp_code
```

This is a simple implementation that just adds some text to the input string and returns it.

Now, let's create the addon code that bridges our C++ code with the Node.js/JavaScript world. Create `src/my_addon.cc`:

```cpp title='src/my_addon.cc'
#include <napi.h>
#include <string>
#include "../include/cpp_code.h"

// Create a class that will be exposed to JavaScript
class MyAddon : public Napi::ObjectWrap<MyAddon> {
public:
    // This static method defines the class for JavaScript
    static Napi::Object Init(Napi::Env env, Napi::Object exports) {
        // Define the JavaScript class with method(s)
        Napi::Function func = DefineClass(env, "MyAddon", {
            InstanceMethod("helloWorld", &MyAddon::HelloWorld)
        });

        // Create a persistent reference to the constructor
        Napi::FunctionReference* constructor = new Napi::FunctionReference();
        *constructor = Napi::Persistent(func);
        env.SetInstanceData(constructor);

        // Set the constructor on the exports object
        exports.Set("MyAddon", func);
        return exports;
    }

    // Constructor
    MyAddon(const Napi::CallbackInfo& info)
        : Napi::ObjectWrap<MyAddon>(info) {}

private:
    // Method that will be exposed to JavaScript
    Napi::Value HelloWorld(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        // Validate arguments (expecting one string)
        if (info.Length() < 1 || !info[0].IsString()) {
            Napi::TypeError::New(env, "Expected string argument").ThrowAsJavaScriptException();
            return env.Null();
        }

        // Convert JavaScript string to C++ string
        std::string input = info[0].As<Napi::String>();

        // Call our C++ function
        std::string result = cpp_code::hello_world(input);

        // Convert C++ string back to JavaScript string and return
        return Napi::String::New(env, result);
    }
};

// Initialize the addon
Napi::Object Init(Napi::Env env, Napi::Object exports) {
    return MyAddon::Init(env, exports);
}

// Register the initialization function
NODE_API_MODULE(my_addon, Init)
```

Let's break down this code:

1. We define a `MyAddon` class that inherits from `Napi::ObjectWrap<MyAddon>`, which handles wrapping our C++ class for JavaScript.
2. The `Init` static method:
  2.1 Defines a JavaScript class with a method called `helloWorld`
  2.2 Creates a persistent reference to the constructor (to prevent garbage collection)
  2.3 Exports the class constructor
3. The constructor simply passes its arguments to the parent class.
4. The `HelloWorld` method:
  4.1 Gets the Napi environment
  4.2 Validates input arguments (expecting a string)
  4.3 Converts the JavaScript string to a C++ string
  4.4 Calls our C++ function
  4.5 Converts the result back to a JavaScript string and returns it
5. We define an initialization function and register it with NODE_API_MODULE macro, which makes our module loadable by Node.js.

Now, let's create a JavaScript wrapper to make the addon easier to use. Create `js/index.js`:

```js title='js/index.js' @ts-expect-error=[5]
const EventEmitter = require('events')

// Load the native addon using the 'bindings' module
// This will look for the compiled .node file in various places
const bindings = require('bindings')
const native = bindings('my_addon')

// Create a nice JavaScript wrapper
class MyNativeAddon extends EventEmitter {
  constructor () {
    super()

    // Create an instance of our C++ class
    this.addon = new native.MyAddon()
  }

  // Wrap the C++ method with a nicer JavaScript API
  helloWorld (input = '') {
    if (typeof input !== 'string') {
      throw new TypeError('Input must be a string')
    }
    return this.addon.helloWorld(input)
  }
}

// Export a singleton instance
if (process.platform === 'win32' || process.platform === 'darwin' || process.platform === 'linux') {
  module.exports = new MyNativeAddon()
} else {
  // Provide a fallback for unsupported platforms
  console.warn('Native addon not supported on this platform')

  module.exports = {
    helloWorld: (input) => `Hello from JS! You said: ${input}`
  }
}
```

This JavaScript wrapper:

1. Uses `bindings` to load our compiled native addon
1. Creates a class that extends EventEmitter (useful for future extensions that might emit events)
1. Instantiates our C++ class and provides a simpler API
1. Adds some input validation on the JavaScript side
1. Exports a singleton instance of our wrapper
1. Handles unsupported platforms gracefully

### Building and testing the addon

Now we can build our native addon:

```sh
npm run build
```

This will run `node-gyp configure` and `node-gyp build` to compile our C++ code into a `.node` file.
Let's create a simple test script to verify everything works. Create `test.js` in the project root:

```js title='test.js' @ts-expect-error=[2]
// Load our addon
const myAddon = require('./js')

// Try the helloWorld function
const result = myAddon.helloWorld('This is a test')

// Should print: "Hello from C++! You said: This is a test"
console.log(result)
```

Run the test:

```sh
node test.js
```

If everything works correctly, you should see:

```txt
Hello from C++! You said: This is a test
```

### Using the addon in Electron

To use this addon in an Electron application, you would:

1. Include it as a dependency in your Electron project
1. Build it targeting your specific Electron version. `electron-forge` handles this step automatically for you - for more details, see [Native Node Modules](./using-native-node-modules.md).
1. Import and use it just like any other module in a process that has Node.js enabled.

```js @ts-expect-error=[2]
// In your main process
const myAddon = require('my-native-addon')
console.log(myAddon.helloWorld('Electron'))
```

## References and further learning

Native addon development can be written in several languages beyond C++. Rust can be used with crates like [`napi-rs`](https://github.com/napi-rs/napi-rs), [`neon`](https://neon-rs.dev/), or [`node-bindgen`](https://github.com/infinyon/node-bindgen). Objective-C/Swift can be used through Objective-C++ on macOS.

The specific implementation details differ significantly by platform, especially when accessing platform-specific APIs or UI frameworks, like Windows' Win32 API, COM components, UWP/WinRT - or macOS's Cocoa, AppKit, or ObjectiveC runtime.

This means that you'll likely use two groups of references for your native code: First, on the Node.js side, use the [N-API documentation](https://nodejs.org/api/n-api.html) to learn about creating and exposing complex structures to JavaScript - like asynchronous thread-safe function calls or creating JavaScript-native objects (`error`, `promise`, etc). Secondly, on the side of the technology you're working with, you'll likely be looking at their lower-level documentation:

* [Microsoft C++, C, and Assembler documentation](https://learn.microsoft.com/en-us/cpp/?view=msvc-170)
* [C++/WinRT](https://learn.microsoft.com/en-us/windows/uwp/cpp-and-winrt-apis/)
* [MSVC-170 C++ Documentation](https://learn.microsoft.com/en-us/cpp/cpp/?view=msvc-170)
* [Apple Developer Documentation](https://developer.apple.com/documentation)
* [Programming with Objective-C](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/ProgrammingWithObjectiveC/Introduction/Introduction.html#//apple_ref/doc/uid/TP40011210)
