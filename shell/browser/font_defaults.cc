// Copyright (c) 2018 Slack Technologies, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/font_defaults.h"

#include <string>
#include <string_view>

#include "base/containers/fixed_flat_map.h"
#include "base/containers/map_util.h"
#include "base/strings/string_split.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/platform_locale_settings.h"
#include "third_party/blink/public/common/web_preferences/web_preferences.h"
#include "ui/base/l10n/l10n_util.h"

namespace {

// The following list of font defaults was copied from
// https://chromium.googlesource.com/chromium/src/+/116.0.5845.228/chrome/browser/ui/prefs/prefs_tab_helper.cc#152
//
// The only updates that should be made to this list are copying updates that
// were made in Chromium.
//
// vvvvv DO NOT EDIT vvvvv

struct FontDefault {
  const char* pref_name;
  int resource_id;
};

// Font pref defaults.  The prefs that have defaults vary by platform, since not
// all platforms have fonts for all scripts for all generic families.
// TODO(falken): add proper defaults when possible for all
// platforms/scripts/generic families.
const FontDefault kFontDefaults[] = {
    {prefs::kWebKitStandardFontFamily, IDS_STANDARD_FONT_FAMILY},
    {prefs::kWebKitFixedFontFamily, IDS_FIXED_FONT_FAMILY},
    {prefs::kWebKitSerifFontFamily, IDS_SERIF_FONT_FAMILY},
    {prefs::kWebKitSansSerifFontFamily, IDS_SANS_SERIF_FONT_FAMILY},
    {prefs::kWebKitCursiveFontFamily, IDS_CURSIVE_FONT_FAMILY},
    {prefs::kWebKitFantasyFontFamily, IDS_FANTASY_FONT_FAMILY},
    {prefs::kWebKitMathFontFamily, IDS_MATH_FONT_FAMILY},
#if BUILDFLAG(IS_CHROMEOS_ASH) || BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
    {prefs::kWebKitStandardFontFamilyJapanese,
     IDS_STANDARD_FONT_FAMILY_JAPANESE},
    {prefs::kWebKitFixedFontFamilyJapanese, IDS_FIXED_FONT_FAMILY_JAPANESE},
    {prefs::kWebKitSerifFontFamilyJapanese, IDS_SERIF_FONT_FAMILY_JAPANESE},
    {prefs::kWebKitSansSerifFontFamilyJapanese,
     IDS_SANS_SERIF_FONT_FAMILY_JAPANESE},
    {prefs::kWebKitStandardFontFamilyKorean, IDS_STANDARD_FONT_FAMILY_KOREAN},
    {prefs::kWebKitSerifFontFamilyKorean, IDS_SERIF_FONT_FAMILY_KOREAN},
    {prefs::kWebKitSansSerifFontFamilyKorean,
     IDS_SANS_SERIF_FONT_FAMILY_KOREAN},
    {prefs::kWebKitStandardFontFamilySimplifiedHan,
     IDS_STANDARD_FONT_FAMILY_SIMPLIFIED_HAN},
    {prefs::kWebKitSerifFontFamilySimplifiedHan,
     IDS_SERIF_FONT_FAMILY_SIMPLIFIED_HAN},
    {prefs::kWebKitSansSerifFontFamilySimplifiedHan,
     IDS_SANS_SERIF_FONT_FAMILY_SIMPLIFIED_HAN},
    {prefs::kWebKitStandardFontFamilyTraditionalHan,
     IDS_STANDARD_FONT_FAMILY_TRADITIONAL_HAN},
    {prefs::kWebKitSerifFontFamilyTraditionalHan,
     IDS_SERIF_FONT_FAMILY_TRADITIONAL_HAN},
    {prefs::kWebKitSansSerifFontFamilyTraditionalHan,
     IDS_SANS_SERIF_FONT_FAMILY_TRADITIONAL_HAN},
#endif
#if BUILDFLAG(IS_MAC) || BUILDFLAG(IS_WIN)
    {prefs::kWebKitCursiveFontFamilySimplifiedHan,
     IDS_CURSIVE_FONT_FAMILY_SIMPLIFIED_HAN},
    {prefs::kWebKitCursiveFontFamilyTraditionalHan,
     IDS_CURSIVE_FONT_FAMILY_TRADITIONAL_HAN},
#endif
#if BUILDFLAG(IS_CHROMEOS_ASH)
    {prefs::kWebKitStandardFontFamilyArabic, IDS_STANDARD_FONT_FAMILY_ARABIC},
    {prefs::kWebKitSerifFontFamilyArabic, IDS_SERIF_FONT_FAMILY_ARABIC},
    {prefs::kWebKitSansSerifFontFamilyArabic,
     IDS_SANS_SERIF_FONT_FAMILY_ARABIC},
    {prefs::kWebKitFixedFontFamilyKorean, IDS_FIXED_FONT_FAMILY_KOREAN},
    {prefs::kWebKitFixedFontFamilySimplifiedHan,
     IDS_FIXED_FONT_FAMILY_SIMPLIFIED_HAN},
    {prefs::kWebKitFixedFontFamilyTraditionalHan,
     IDS_FIXED_FONT_FAMILY_TRADITIONAL_HAN},
#elif BUILDFLAG(IS_WIN)
    {prefs::kWebKitFixedFontFamilyArabic, IDS_FIXED_FONT_FAMILY_ARABIC},
    {prefs::kWebKitSansSerifFontFamilyArabic,
     IDS_SANS_SERIF_FONT_FAMILY_ARABIC},
    {prefs::kWebKitStandardFontFamilyCyrillic,
     IDS_STANDARD_FONT_FAMILY_CYRILLIC},
    {prefs::kWebKitFixedFontFamilyCyrillic, IDS_FIXED_FONT_FAMILY_CYRILLIC},
    {prefs::kWebKitSerifFontFamilyCyrillic, IDS_SERIF_FONT_FAMILY_CYRILLIC},
    {prefs::kWebKitSansSerifFontFamilyCyrillic,
     IDS_SANS_SERIF_FONT_FAMILY_CYRILLIC},
    {prefs::kWebKitStandardFontFamilyGreek, IDS_STANDARD_FONT_FAMILY_GREEK},
    {prefs::kWebKitFixedFontFamilyGreek, IDS_FIXED_FONT_FAMILY_GREEK},
    {prefs::kWebKitSerifFontFamilyGreek, IDS_SERIF_FONT_FAMILY_GREEK},
    {prefs::kWebKitSansSerifFontFamilyGreek, IDS_SANS_SERIF_FONT_FAMILY_GREEK},
    {prefs::kWebKitFixedFontFamilyKorean, IDS_FIXED_FONT_FAMILY_KOREAN},
    {prefs::kWebKitCursiveFontFamilyKorean, IDS_CURSIVE_FONT_FAMILY_KOREAN},
    {prefs::kWebKitFixedFontFamilySimplifiedHan,
     IDS_FIXED_FONT_FAMILY_SIMPLIFIED_HAN},
    {prefs::kWebKitFixedFontFamilyTraditionalHan,
     IDS_FIXED_FONT_FAMILY_TRADITIONAL_HAN},
#endif
};

// ^^^^^ DO NOT EDIT ^^^^^

auto MakeDefaultFontCopier() {
  using namespace prefs;
  using WP = blink::web_pref::WebPreferences;
  using FamilyMap = blink::web_pref::ScriptFontFamilyMap;

  // Map from a family name (e.g. "webkit.webprefs.fonts.fixed") to a
  // Pointer-to-Member of the location in WebPreferences of its
  // ScriptFontFamilyMap (e.g. &WebPreferences::fixed_font_family_map)
  static constexpr auto FamilyMapByName =
      base::MakeFixedFlatMap<std::string_view, FamilyMap WP::*>({
          {kWebKitStandardFontFamilyMap, &WP::standard_font_family_map},
          {kWebKitFixedFontFamilyMap, &WP::fixed_font_family_map},
          {kWebKitSerifFontFamilyMap, &WP::serif_font_family_map},
          {kWebKitSansSerifFontFamilyMap, &WP::sans_serif_font_family_map},
          {kWebKitCursiveFontFamilyMap, &WP::cursive_font_family_map},
      });

  WP defaults;

  // Populate `defaults`'s ScriptFontFamilyMaps with the values from
  // the kFontDefaults array in the "DO NOT EDIT" section of this file.
  //
  // The kFontDefaults's `pref_name` field is built as `${family}.${script}`,
  // so splitting on the last '.' gives the family and script: a pref key of
  // "webkit.webprefs.fonts.fixed.Zyyy" splits into family name
  // "webkit.webprefs.fonts.fixed" and script "Zyyy". (Yes, "Zyyy" is real.
  // See pref_font_script_names-inl.h for the full list :)
  for (const auto& [pref_name, resource_id] : kFontDefaults) {
    const auto [family, script] = *base::RSplitStringOnce(pref_name, '.');
    if (auto* family_map_ptr = base::FindOrNull(FamilyMapByName, family)) {
      FamilyMap& family_map = defaults.**family_map_ptr;
      family_map[std::string{script}] = l10n_util::GetStringUTF16(resource_id);
    }
  }

  // Lambda that copies all of `default`'s fonts into `prefs`
  auto copy_default_fonts_to_web_prefs = [defaults](WP* prefs) {
    for (const auto [_, family_map_ptr] : FamilyMapByName) {
      const FamilyMap& src = defaults.*family_map_ptr;
      FamilyMap& tgt = prefs->*family_map_ptr;
      for (const auto& [key, val] : src)
        tgt[key] = val;
    }
  };

  return copy_default_fonts_to_web_prefs;
}
}  // namespace

namespace electron {

void SetFontDefaults(blink::web_pref::WebPreferences* prefs) {
  static const auto copy_default_fonts_to_web_prefs = MakeDefaultFontCopier();
  copy_default_fonts_to_web_prefs(prefs);
}

}  // namespace electron
