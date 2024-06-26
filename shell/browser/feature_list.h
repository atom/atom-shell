// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ELECTRON_SHELL_BROWSER_FEATURE_LIST_H_
#define ELECTRON_SHELL_BROWSER_FEATURE_LIST_H_

#include <string>

namespace electron {
void InitializeFeatureList();
void InitializeFieldTrials();
std::string EnablePlatformSpecificFeatures();
}  // namespace electron

#endif  // ELECTRON_SHELL_BROWSER_FEATURE_LIST_H_
