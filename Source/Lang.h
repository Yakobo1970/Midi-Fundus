// MIDIARCHIV – user interface languages (Deutsch, English, Français).
// Texts are written in German in the code; T() looks up the translation. Placeholders %1 … %9.
// Analysis values stay German in the database (keys, scales, languages) and are translated for display.
#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

namespace ma
{
enum class Lang { de = 0, en = 1, fr = 2 };

Lang language();
void setLanguage (Lang);
juce::String langCode (Lang);                        // "de", "en", "fr"
bool langFromCode (const juce::String& code, Lang&); // also "german", "english", "french" (Inno Setup)
void initLanguage (juce::ValueTree& settings);       // settings, else setup choice (registry), else Windows language
void chooseLanguage (juce::ValueTree& settings, Lang);

juce::String T (const char* germanUtf8);
juce::String TS (const juce::String& german);        // runtime strings (stored error texts …)

template <typename... Args>
juce::String T (const char* germanUtf8, const Args&... args)
{
    auto s = T (germanUtf8);
    int i = 1;
    ((s = s.replace ("%" + juce::String (i++), juce::String (args))), ...);
    return s;
}

// analysis values (stored in German) for display
juce::String localKey (const juce::String& germanKey);       // "fis-Moll?" -> "F# minor?" / "fa# mineur?"
juce::String localMode (const juce::String& germanKey);      // "Dur" / "Moll" / ""
juce::String localScale (const juce::String& germanScale);   // "Diatonisch · 7" -> "Diatonic · 7"
juce::String localLanguageName (const juce::String& germanName);
juce::String localDate (juce::int64 millis);
juce::String keyInputToGerman (const juce::String& input);   // typed key in the current language -> "fis-Moll"
} // namespace ma
