// VanePresetExport — convert ~/.config/Vane/Presets/*.vanepreset (Linux) or
// ~/Library/Vane/Presets/*.vanepreset (macOS) into MODEP-style LV2 preset
// bundles, one <Vane-Name>.lv2/ directory each (manifest.ttl + <slug>.ttl).
//
// The state blob is produced by the *real* VaneProcessor::getStateInformation()
// run through MemoryBlock::toBase64Encoding(), so it is byte-identical to what
// the JUCE LV2 wrapper writes when MODEP saves a preset — and therefore loads
// back via setStateInformation() with no format guessing.
//
// Usage:
//   VanePresetExport [outputDir] [pluginURI]
//     outputDir  default: $HOME/Vane-MODEP-Presets
//     pluginURI  default: https://enkerli.com/plugins/Vane
//
// Then on the Pi:
//   sudo cp -r <outputDir>/*.lv2 /var/modep/lv2-presets/
//   sudo systemctl restart modep-mod-ui modep-mod-host
//
// Build:
//   cmake --build build --target VanePresetExport
//   ./build/VanePresetExport_artefacts/<cfg>/VanePresetExport
#include "../../Source/PluginProcessor.h"
#include <cstdio>

// Turtle string-literal escaping for the rdfs:label (handles " and \).
static juce::String ttlEscape (const juce::String& s)
{
    return s.replace ("\\", "\\\\").replace ("\"", "\\\"");
}

// Filesystem-safe slug for the bundle dir + .ttl filename. Keeps [A-Za-z0-9._-],
// everything else → '_'.  "Breath Chord Lead 1" → "Breath_Chord_Lead_1".
static juce::String slugify (const juce::String& s)
{
    juce::String out;
    for (auto c : s)
        out += (juce::CharacterFunctions::isLetterOrDigit (c)
                || c == '.' || c == '_' || c == '-') ? juce::String::charToString (c)
                                                      : juce::String ("_");
    return out.isEmpty() ? juce::String ("preset") : out;
}

static const char* kPrefixes =
    "@prefix atom: <http://lv2plug.in/ns/ext/atom#> .\n"
    "@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n"
    "@prefix pset: <http://lv2plug.in/ns/ext/presets#> .\n"
    "@prefix rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .\n"
    "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n"
    "@prefix state: <http://lv2plug.in/ns/ext/state#> .\n"
    "@prefix xsd: <http://www.w3.org/2001/XMLSchema#> .\n\n";

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // PresetManager touches File locations

    const juce::File outDir = (argc > 1)
        ? juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[1]))
        : juce::File::getSpecialLocation (juce::File::userHomeDirectory)
              .getChildFile ("Vane-MODEP-Presets");
    const juce::String uri = (argc > 2) ? juce::String (argv[2])
                                        : juce::String ("https://enkerli.com/plugins/Vane");

    outDir.createDirectory();

    VaneProcessor proc;   // reused: loadPreset() fully replaces the APVTS state
    const auto names = proc.presetManager.getPresetNames();

    std::printf ("Exporting %d preset(s) → %s\n  pluginURI: %s\n",
                 names.size(), outDir.getFullPathName().toRawUTF8(), uri.toRawUTF8());

    int ok = 0;
    for (const auto& name : names)
    {
        proc.presetManager.loadPreset (name);

        juce::MemoryBlock mb;
        proc.getStateInformation (mb);
        const juce::String stateStr = mb.toBase64Encoding();

        const juce::String slug   = slugify (name);
        const juce::String ttlNm  = slug + ".ttl";
        const juce::File   bundle = outDir.getChildFile ("Vane-" + slug + ".lv2");
        bundle.createDirectory();

        // manifest.ttl — points to the preset file in this bundle.
        juce::String manifest = kPrefixes;
        manifest << "<" << ttlNm << ">\n"
                 << "\tlv2:appliesTo <" << uri << "> ;\n"
                 << "\ta pset:Preset ;\n"
                 << "\trdfs:seeAlso <" << ttlNm << "> .\n";
        bundle.getChildFile ("manifest.ttl").replaceWithText (manifest);

        // <slug>.ttl — the preset itself: label + full state blob.
        juce::String preset = kPrefixes;
        preset << "<>\n"
               << "\ta pset:Preset ;\n"
               << "\tlv2:appliesTo <" << uri << "> ;\n"
               << "\trdfs:label \"" << ttlEscape (name) << "\" ;\n"
               << "\tlv2:port [\n"
               << "\t\tlv2:symbol \"enabled\" ;\n"
               << "\t\tpset:value 1.0\n"
               << "\t] , [\n"
               << "\t\tlv2:symbol \"freeWheeling\" ;\n"
               << "\t\tpset:value 0.0\n"
               << "\t] ;\n"
               << "\tstate:state [\n"
               << "\t\t<" << uri << ":StateString> \"" << stateStr << "\"\n"
               << "\t] .\n";
        bundle.getChildFile (ttlNm).replaceWithText (preset);

        std::printf ("  wrote  %s\n", bundle.getFileName().toRawUTF8());
        ++ok;
    }

    std::printf ("Done: %d/%d bundle(s).\n"
                 "Install with:\n"
                 "  sudo cp -r %s/*.lv2 /var/modep/lv2-presets/\n"
                 "  sudo systemctl restart modep-mod-ui modep-mod-host\n",
                 ok, names.size(), outDir.getFullPathName().toRawUTF8());
    return ok == names.size() ? 0 : 1;
}
