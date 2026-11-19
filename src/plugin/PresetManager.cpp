#include "PresetManager.h"

#include "PluginProcessor.h"
#include "PluginParameters.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace hypercolor::plugin
{
namespace
{
struct FactoryRow { const char* category; const char* name; const char* description; };
constexpr std::array<FactoryRow, 50> factoryRows {{
    {"Lead", "Glasswire Lead", "Bright, wide lead with a controlled spatial tail."},
    {"Lead", "Soft Signal", "A rounded lead with gentle modulation and restrained width."},
    {"Lead", "Voltage Bloom", "An assertive driven lead with a moving upper edge."},
    {"Lead", "Neon Thread", "A fine granular shimmer over a stable melodic center."},
    {"Lead", "Fracture Line", "Short, syncopated grain motion with clipped repeats."},
    {"Lead", "Cloud Runner", "Airy stereo movement for sustained phrases."},
    {"Lead", "Iron Choir", "Dense, harmonically rich lead with a compact low end."},
    {"Pluck", "Afterimage", "A pluck with a long, diffuse but quiet tail."},
    {"Pluck", "Fifth Dust", "Light pitch drift and short granular echoes."},
    {"Pluck", "Clockwork Seed", "Rhythmic repeats and a crisp transient focus."},
    {"Pluck", "Velvet Pin", "Softened attack with a close, warm space."},
    {"Pluck", "Pixel Harp", "A bright digital pluck with restrained bit reduction."},
    {"Synth", "Wide Current", "Broad chorus movement with a stable center."},
    {"Synth", "Copper Halo", "Warm drive and a short room around sustained chords."},
    {"Synth", "Prism Stack", "Layered motion with two distinct modulation stages."},
    {"Synth", "Static Garden", "A measured digital edge for harmonic material."},
    {"Synth", "Soft Machines", "Slow modulation and gentle tape movement."},
    {"Synth", "Night Circuit", "A darker filtered synth texture with controlled repeats."},
    {"Atmosphere", "Far Meridian", "Long, distant space with slow evolving grains."},
    {"Atmosphere", "Blue Vapor", "A smooth, airy cloud with subtle pitch movement."},
    {"Atmosphere", "Memory Foam", "Soft modulation and softened high frequencies."},
    {"Atmosphere", "Orbiting Ash", "A drifting stereo field with a quiet echo trail."},
    {"Atmosphere", "Glass Weather", "Shimmering, fragile texture with gentle spectral motion."},
    {"Atmosphere", "Low Satellites", "A dark, slow texture with restrained low-frequency build."},
    {"Pad", "Slow Bloom", "A wide pad that opens gradually without a level jump."},
    {"Pad", "Tidal Room", "Deep space and slow, even modulation."},
    {"Pad", "Moss Light", "A soft, warm pad with a close tonal focus."},
    {"Pad", "Polar Veil", "Wide, cool movement with a long but controlled decay."},
    {"Pad", "Held Breath", "A quiet, smooth sustain with minimal pitch drift."},
    {"Kick", "Low Anchor", "Low-end focused; modest processing protects the punch."},
    {"Kick", "Rust Hammer", "A compact drive texture with a firm fundamental."},
    {"Kick", "Sub Orbit", "Sub weight with a short, low-cut echo."},
    {"Kick", "Granite Pulse", "A dense kick texture with limited stereo movement."},
    {"Snare", "Paper Halo", "A short bright tail around the snare transient."},
    {"Snare", "Wire Room", "A tight room and a little metallic modulation."},
    {"Snare", "Dust Break", "Granular roughness with moderate drive."},
    {"Snare", "Silver Snap", "A clean transient with a wide, short echo."},
    {"Drums", "Pocket Sparks", "Light rhythmic delay for a drum bus."},
    {"Drums", "Room Language", "A compact room and subtle bus compression."},
    {"Drums", "Broken Meter", "Controlled bit reduction and syncopated movement."},
    {"Drums", "Soft Parallel", "A restrained parallel-style compression character."},
    {"Drums", "Metal Orchard", "Bright, modulated percussion with a short tail."},
    {"Vocal", "Close Orbit", "Intimate vocal space with modest stereo width."},
    {"Vocal", "Velour Doubler", "Gentle modulation for a soft doubled impression."},
    {"Vocal", "Chorus Bloom", "An airy vocal cloud with controlled reverb."},
    {"Vocal", "Cutout Choir", "Grain movement and short digital repeats for chops."},
    {"Bass", "Carbon Thread", "Controlled drive with a steady mono-centered low end."},
    {"Bass", "Subgrain", "Slow grain motion and restrained tape color."},
    {"FX", "Event Horizon", "Extreme but level-conscious transformation for transitions."},
    {"FX", "Glass Collapse", "Digital fracture, pitch movement and a long spatial tail."}
}};

juce::String factoryIdForIndex(std::size_t index)
{
    return juce::String::formatted("factory.%02d", static_cast<int>(index + 1));
}

constexpr std::array<std::array<FxType, 4>, 11> categoryChains {{
    {{FxType::space, FxType::modulation, FxType::delay, FxType::drive}},
    {{FxType::space, FxType::delay, FxType::bitcrush, FxType::modulation}},
    {{FxType::modulation, FxType::drive, FxType::space, FxType::filter}},
    {{FxType::space, FxType::tape, FxType::modulation, FxType::delay}},
    {{FxType::space, FxType::modulation, FxType::tape, FxType::filter}},
    {{FxType::drive, FxType::filter, FxType::dynamics, FxType::delay}},
    {{FxType::space, FxType::drive, FxType::bitcrush, FxType::modulation}},
    {{FxType::dynamics, FxType::space, FxType::delay, FxType::bitcrush}},
    {{FxType::modulation, FxType::space, FxType::delay, FxType::filter}},
    {{FxType::drive, FxType::tape, FxType::filter, FxType::dynamics}},
    {{FxType::bitcrush, FxType::modulation, FxType::delay, FxType::space}}
}};

std::size_t categoryIndex(const juce::String& category)
{
    const auto& categories = PresetManager::factoryCategories();
    for (int i = 0; i < categories.size(); ++i)
        if (categories[i] == category) return static_cast<std::size_t>(i);
    return 0;
}

bool setRaw(HypercolorAudioProcessor& processor, const juce::String& id, float value)
{
    auto* parameter = processor.getValueTreeState().getParameter(id);
    auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
    if (ranged == nullptr || !std::isfinite(value)) return false;
    ranged->setValueNotifyingHost(ranged->convertTo0to1(value));
    return true;
}

juce::String legacyFxParameter(FxType type, std::size_t n)
{
    static constexpr const char* ids[8][9] {
        {"reverbMix", "reverbSize", "reverbDecay", "reverbDamping", "reverbPreDelay", "reverbWidth"},
        {"modulationMode", "modulationRate", "modulationDepth", "modulationBaseDelay", "modulationFeedback", "modulationMix", "modulationWidth"},
        {"driveMode", "driveAmount", "driveTone", "driveMix", "driveOutput"},
        {"tapeDrive", "tapeWow", "tapeFlutter", "tapeWear", "tapeMix", "tapeOutput"},
        {"bitcrushBits", "bitcrushRate", "bitcrushMode", "bitcrushMix", "bitcrushOutput"},
        {"filterMode", "filterCutoff", "filterResonance", "filterDrive", "filterMix", "filterOutput"},
        {"compressorThreshold", "compressorRatio", "compressorAttack", "compressorRelease", "compressorKnee", "compressorMakeup", "compressorMix", "compressorStereoLink"},
        {"delayMode", "delaySync", "delayTime", "delayDivision", "delayFeedback", "delayLowCut", "delayHighCut", "delayMix", "delayWidth"}
    };
    const auto t = toIndex(type);
    return t < 8 && n < 9 && ids[t][n] != nullptr ? ids[t][n] : "";
}

void setFxValue(HypercolorAudioProcessor& processor, const FxInstanceId& id,
                std::size_t parameterIndex, float value)
{
    const auto legacy = legacyFxParameter(id.type, parameterIndex);
    if (legacy.isNotEmpty())
        (void) setRaw(processor, fxParameterID(id.type, id.slot, legacy.toRawUTF8()), value);
}

struct FactoryStateBuilder
{
    HypercolorAudioProcessor processor;
    juce::MemoryBlock defaultState;

    FactoryStateBuilder() { processor.getStateInformation(defaultState); }
    void reset()
    {
        processor.setStateInformation(defaultState.getData(), static_cast<int>(defaultState.getSize()));
        auto& state = processor.getValueTreeState().state;
        for (const auto* nodeName : { "FX_CHAIN", "FX_MACRO_ASSIGNMENTS", "LFO_CONFIGURATION" })
        {
            const auto child = state.getChildWithName(nodeName);
            if (child.isValid()) state.removeChild(child, nullptr);
        }
        for (const auto* property : { "schemaVersion", "nextFxIdentity", "nextLfoIdentity" })
            state.removeProperty(property, nullptr);
    }
};
}

PresetManager::PresetManager(HypercolorAudioProcessor& processorToUse, juce::File directory)
    : processor(processorToUse),
      userRoot(directory == juce::File{} ? juce::File::getSpecialLocation(
          juce::File::userApplicationDataDirectory).getChildFile("HYPERCOLOR").getChildFile("Presets") : directory),
      favoritesFile(userRoot.getParentDirectory().getChildFile("preset-favorites.xml"))
{
    loadFavorites();
}

const juce::StringArray& PresetManager::factoryCategories()
{
    static const juce::StringArray categories { "Lead", "Pluck", "Synth", "Atmosphere", "Pad",
        "Kick", "Snare", "Drums", "Vocal", "Bass", "FX" };
    return categories;
}

std::size_t PresetManager::factoryCount() noexcept { return factoryRows.size(); }

std::vector<PresetInfo> PresetManager::getPresets() const
{
    std::vector<PresetInfo> result;
    result.reserve(factoryRows.size() + 32);
    for (std::size_t i = 0; i < factoryRows.size(); ++i)
    {
        const auto id = factoryIdForIndex(i);
        result.push_back({ id, factoryRows[i].name, factoryRows[i].category,
            factoryRows[i].description, true, favoriteIds.contains(id) });
    }
    juce::Array<juce::File> files;
    userRoot.findChildFiles(files, juce::File::findFiles, false, "*.hxpreset");
    for (const auto& file : files)
    {
        PresetInfo info; juce::MemoryBlock ignored;
        if (readUserPreset(file.getFileNameWithoutExtension(), info, ignored))
        {
            info.favorite = favoriteIds.contains(info.id);
            result.push_back(std::move(info));
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b)
    {
        if (a.factory != b.factory) return a.factory;
        return a.name.compareIgnoreCase(b.name) < 0;
    });
    return result;
}

juce::MemoryBlock PresetManager::captureCurrentState() const
{
    juce::MemoryBlock state; processor.getStateInformation(state); return state;
}

bool PresetManager::isModified() const
{
    if (loadedState.getSize() == 0) return false;
    const auto current = captureCurrentState();
    return current.getSize() != loadedState.getSize()
        || std::memcmp(current.getData(), loadedState.getData(), current.getSize()) != 0;
}

void PresetManager::rememberLoaded(const PresetInfo& info, const juce::MemoryBlock& state)
{
    loadedId = info.id; loadedName = info.name; loadedCategory = info.category;
    loadedFactory = info.factory;
    juce::ignoreUnused(state);
    loadedState = captureCurrentState();
}

bool PresetManager::applyState(const juce::MemoryBlock& state)
{
    if (state.getSize() == 0 || state.getSize() > 4 * 1024 * 1024) return false;
    const auto xml = processor.getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
    if (xml == nullptr || !xml->hasTagName("HYPERCOLOR_STATE")) return false;
    processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    return true;
}

bool PresetManager::load(const juce::String& id)
{
    PresetInfo info; juce::MemoryBlock state;
    for (std::size_t i = 0; i < factoryRows.size(); ++i)
    {
        const auto factoryId = factoryIdForIndex(i);
        if (factoryId == id)
        {
            info = { factoryId, factoryRows[i].name, factoryRows[i].category,
                factoryRows[i].description, true, favoriteIds.contains(factoryId) };
            state = makeFactoryState(factoryId);
            break;
        }
    }
    if (state.getSize() == 0 && !readUserPreset(id, info, state)) return false;
    if (!applyState(state)) return false;
    rememberLoaded(info, state);
    return true;
}

bool PresetManager::saveAs(const juce::String& name, const juce::String& category,
                           const juce::String& description)
{
    if (!isSafeName(name) || !factoryCategories().contains(category)) return false;
    if (!userRoot.createDirectory()) return false;
    PresetInfo info { juce::Uuid().toString(), name.trim(), category, description.trim(), false, false };
    const auto state = captureCurrentState();
    if (!writeUserPreset(info, state)) return false;
    rememberLoaded(info, state); return true;
}

bool PresetManager::overwrite(const juce::String& id)
{
    if (id.startsWith("factory.")) return false;
    PresetInfo info; juce::MemoryBlock existing;
    if (!readUserPreset(id, info, existing)) return false;
    const auto state = captureCurrentState();
    if (!writeUserPreset(info, state)) return false;
    rememberLoaded(info, state); return true;
}

bool PresetManager::rename(const juce::String& id, const juce::String& newName)
{
    if (id.startsWith("factory.") || !isSafeName(newName)) return false;
    PresetInfo info; juce::MemoryBlock state;
    if (!readUserPreset(id, info, state)) return false;
    info.name = newName.trim();
    if (!writeUserPreset(info, state)) return false;
    if (loadedId == id) rememberLoaded(info, state);
    return true;
}

bool PresetManager::erase(const juce::String& id)
{
    if (id.startsWith("factory.")) return false;
    const auto file = fileForId(id);
    if (!file.existsAsFile() || !file.deleteFile()) return false;
    favoriteIds.removeString(id); saveFavorites();
    if (loadedId == id) { loadedId.clear(); loadedName = "Init"; loadedCategory.clear(); loadedState.reset(); }
    return true;
}

bool PresetManager::setFavorite(const juce::String& id, bool favorite)
{
    const bool factory = id.startsWith("factory.");
    if (factory)
    {
        const auto index = id.fromLastOccurrenceOf(".", false, false).getIntValue() - 1;
        if (index < 0 || index >= static_cast<int>(factoryRows.size())) return false;
    }
    else { PresetInfo info; juce::MemoryBlock state; if (!readUserPreset(id, info, state)) return false; }
    favoriteIds.removeString(id);
    if (favorite) favoriteIds.add(id);
    saveFavorites(); return true;
}

bool PresetManager::isSafeName(const juce::String& name)
{
    const auto trimmed = name.trim();
    return trimmed.isNotEmpty() && trimmed.length() <= 80
        && !trimmed.containsAnyOf("/\\:\0") && trimmed != "." && trimmed != "..";
}

juce::File PresetManager::fileForId(const juce::String& id) const
{
    if (id.isEmpty() || id.containsAnyOf("/\\")) return {};
    return userRoot.getChildFile(id + ".hxpreset");
}

bool PresetManager::readUserPreset(const juce::String& id, PresetInfo& info,
                                   juce::MemoryBlock& state) const
{
    const auto file = fileForId(id);
    if (!file.existsAsFile() || file.getSize() > 4 * 1024 * 1024) return false;
    const auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || !xml->hasTagName("HYPERCOLOR_PRESET")) return false;
    const auto* wrapper = xml->getChildByName("PLUGIN_STATE");
    if (wrapper == nullptr || !wrapper->hasTagName("PLUGIN_STATE")) return false;
    const auto* child = wrapper->getFirstChildElement();
    if (child == nullptr) return false;
    const auto presetId = xml->getStringAttribute("id");
    const auto name = xml->getStringAttribute("name");
    const auto category = xml->getStringAttribute("category");
    if (presetId != id || !isSafeName(name) || !factoryCategories().contains(category)) return false;
    juce::MemoryBlock binary; processor.copyXmlToBinary(*child, binary);
    const auto stateXml = processor.getXmlFromBinary(binary.getData(), static_cast<int>(binary.getSize()));
    if (stateXml == nullptr || !stateXml->hasTagName("HYPERCOLOR_STATE")) return false;
    info = { id, name, category, xml->getStringAttribute("description"), false, false };
    state = binary;
    return state.getSize() > 0;
}

bool PresetManager::writeUserPreset(const PresetInfo& info, const juce::MemoryBlock& state) const
{
    if (!userRoot.createDirectory() || state.getSize() == 0) return false;
    auto xml = processor.getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
    if (xml == nullptr) return false;
    juce::XmlElement root("HYPERCOLOR_PRESET");
    root.setAttribute("id", info.id); root.setAttribute("name", info.name);
    root.setAttribute("category", info.category); root.setAttribute("description", info.description);
    auto* stateWrapper = root.createNewChildElement("PLUGIN_STATE");
    stateWrapper->addChildElement(xml.release());
    const auto target = fileForId(info.id);
    return target.replaceWithText(root.toString());
}

void PresetManager::loadFavorites()
{
    favoriteIds.clear();
    if (!favoritesFile.existsAsFile()) return;
    const auto xml = juce::XmlDocument::parse(favoritesFile);
    if (xml == nullptr || !xml->hasTagName("HYPERCOLOR_FAVORITES")) return;
    for (auto* child : xml->getChildIterator())
        if (child->hasTagName("FAVORITE")) favoriteIds.addIfNotAlreadyThere(child->getStringAttribute("id"));
}

void PresetManager::saveFavorites() const
{
    auto root = std::make_unique<juce::XmlElement>("HYPERCOLOR_FAVORITES");
    for (const auto& id : favoriteIds) { auto* item = root->createNewChildElement("FAVORITE"); item->setAttribute("id", id); }
    const auto parent = favoritesFile.getParentDirectory();
    (void) parent.createDirectory();
    (void) favoritesFile.replaceWithText(root->toString());
}

juce::MemoryBlock PresetManager::makeFactoryState(const juce::String& id) const
{
    const auto rowIndex = id.fromLastOccurrenceOf(".", false, false).getIntValue() - 1;
    if (rowIndex < 0 || rowIndex >= static_cast<int>(factoryRows.size())) return {};
    static FactoryStateBuilder builder;
    static std::array<juce::MemoryBlock, factoryRows.size()> stateCache;
    auto& cached = stateCache[static_cast<std::size_t>(rowIndex)];
    if (cached.getSize() > 0) return cached;
    builder.reset();
    auto& factoryProcessor = builder.processor;
    const auto& row = factoryRows[static_cast<std::size_t>(rowIndex)];
    const auto cat = categoryIndex(row.category);
    const auto itemInCategory = [&]
    {
        int count = 0;
        for (int i = 0; i < rowIndex; ++i) if (juce::String(factoryRows[static_cast<std::size_t>(i)].category) == row.category) ++count;
        return count;
    }();
    const auto variant = itemInCategory;
    const auto variantIndex = static_cast<std::size_t>(variant);
    const auto variantFloat = static_cast<float>(variant);

    static constexpr float sizes[5] { 35.0f, 70.0f, 115.0f, 185.0f, 280.0f };
    static constexpr float densities[5] { 18.0f, 28.0f, 42.0f, 64.0f, 92.0f };
    static constexpr float positions[5] { 0.12f, 0.31f, 0.52f, 0.73f, 0.88f };
    static constexpr float pitches[5] { -7.0f, -2.0f, 0.0f, 3.0f, 7.0f };
    auto set = [](const char* parameter, float value)
    { (void) setRaw(factoryProcessor, parameter, value); };
    set(ParameterIDs::dryWet, cat >= 5 ? 82.0f : 88.0f);
    set(ParameterIDs::outputGain, cat == 10 ? -5.0f : -2.0f);
    set(ParameterIDs::grainDensity, densities[(variantIndex + cat) % 5]);
    set(ParameterIDs::grainSize, sizes[(variantIndex * 2 + cat) % 5]);
    set(ParameterIDs::grainPosition, positions[(variantIndex + cat * 2) % 5]);
    set(ParameterIDs::grainPitch, pitches[(variantIndex + cat) % 5]);
    set(ParameterIDs::grainReverse, ((cat == 3 || cat == 10) && (variant % 2 == 1)) ? 1.0f : 0.0f);
    set(ParameterIDs::grainDensityMode, cat == 7 && variant % 2 == 1 ? 1.0f : 0.0f);
    set(ParameterIDs::grainDensityDivision, static_cast<float>((variant + 2) % 9));
    set(ParameterIDs::grainPitchRandom, cat == 10 ? 22.0f : (cat == 3 ? 11.0f : 5.0f));
    set(ParameterIDs::grainPositionRandom, cat == 10 ? 30.0f : (cat == 3 ? 18.0f : 7.0f));
    set(ParameterIDs::grainTimingJitter, cat == 8 || cat == 10 ? 15.0f : 4.0f);
    set(ParameterIDs::grainStereoSpread, cat == 5 || cat == 9 ? 15.0f : 60.0f);
    set(ParameterIDs::grainPanRandom, cat == 9 ? 8.0f : 28.0f);

    FxChainConfiguration chain;
    const auto chainLength = cat == 5 || cat == 9 ? 2 : 3;
    for (int i = 0; i < chainLength; ++i)
    {
        const auto type = (i == 2 && rowIndex % 13 == 4 && chain.count > 0)
            ? chain.order[0].type
            : categoryChains[cat][static_cast<std::size_t>((i + variant) % 4)];
        std::uint8_t slot = 0;
        for (std::size_t n = 0; n < chain.count; ++n) if (chain.order[n].type == type) ++slot;
        if (slot >= maximumFxInstancesPerType || chain.count >= maximumFxChainModules) continue;
        FxInstanceId instance { type, slot, 10000u + static_cast<std::uint64_t>(rowIndex) * 10u + chain.count + 1u };
        chain.order[chain.count++] = instance;
        static constexpr const char* enabledIds[] { "reverbEnabled", "modulationEnabled",
            "driveEnabled", "tapeEnabled", "bitcrushEnabled", "filterEnabled",
            "compressorEnabled", "delayEnabled" };
        const auto enabledId = fxEnabledParameterID(type, slot, enabledIds[toIndex(type)]);
        setRaw(factoryProcessor, enabledId, 1.0f);
        switch (type)
        {
            case FxType::space:
                setFxValue(factoryProcessor, instance, 0, cat == 3 ? 24.0f : 20.0f + variantFloat * 5.0f);
                setFxValue(factoryProcessor, instance, 1, 38.0f + variantFloat * 9.0f);
                setFxValue(factoryProcessor, instance, 2, cat == 3 || cat == 4 ? 7.0f : 2.8f + variantFloat * 0.5f);
                setFxValue(factoryProcessor, instance, 3, 48.0f);
                setFxValue(factoryProcessor, instance, 4, cat == 3 ? 36.0f : 12.0f);
                setFxValue(factoryProcessor, instance, 5, cat == 9 ? 100.0f : 125.0f);
                break;
            case FxType::modulation:
                setFxValue(factoryProcessor, instance, 0, variant % 3 == 0 ? 1.0f : 0.0f);
                setFxValue(factoryProcessor, instance, 1, 0.15f + 0.11f * static_cast<float>(variant + 1));
                setFxValue(factoryProcessor, instance, 2, 24.0f + variantFloat * 8.0f);
                setFxValue(factoryProcessor, instance, 3, 5.0f);
                setFxValue(factoryProcessor, instance, 4, variant % 2 ? 12.0f : -8.0f);
                setFxValue(factoryProcessor, instance, 5, cat == 8 ? 22.0f : 30.0f);
                setFxValue(factoryProcessor, instance, 6, 120.0f);
                break;
            case FxType::drive:
                setFxValue(factoryProcessor, instance, 0, variant % 3 == 0 ? 2.0f : 0.0f);
                setFxValue(factoryProcessor, instance, 1, cat == 9 ? 3.0f : 5.0f + variantFloat * 2.0f);
                setFxValue(factoryProcessor, instance, 2, 52.0f);
                setFxValue(factoryProcessor, instance, 3, 70.0f);
                setFxValue(factoryProcessor, instance, 4, -2.0f);
                break;
            case FxType::tape:
                setFxValue(factoryProcessor, instance, 0, 24.0f + variantFloat * 7.0f);
                setFxValue(factoryProcessor, instance, 1, 12.0f + variantFloat * 4.0f);
                setFxValue(factoryProcessor, instance, 2, 8.0f + variantFloat * 3.0f);
                setFxValue(factoryProcessor, instance, 3, 20.0f + variantFloat * 5.0f);
                setFxValue(factoryProcessor, instance, 4, 28.0f);
                setFxValue(factoryProcessor, instance, 5, -1.0f);
                break;
            case FxType::bitcrush:
                setFxValue(factoryProcessor, instance, 0, cat == 10 ? 10.0f : 12.0f - variantFloat);
                setFxValue(factoryProcessor, instance, 1, 18000.0f - variantFloat * 1200.0f);
                setFxValue(factoryProcessor, instance, 2, variant % 2 ? 1.0f : 0.0f);
                setFxValue(factoryProcessor, instance, 3, 28.0f);
                setFxValue(factoryProcessor, instance, 4, -2.0f);
                break;
            case FxType::filter:
                setFxValue(factoryProcessor, instance, 0, 0.0f);
                setFxValue(factoryProcessor, instance, 1, cat == 9 ? 5000.0f : 11000.0f - variantFloat * 1000.0f);
                setFxValue(factoryProcessor, instance, 2, 25.0f + variantFloat * 6.0f);
                setFxValue(factoryProcessor, instance, 3, cat == 9 ? 3.0f : 0.5f);
                setFxValue(factoryProcessor, instance, 4, 70.0f);
                setFxValue(factoryProcessor, instance, 5, -1.0f);
                break;
            case FxType::dynamics:
                setFxValue(factoryProcessor, instance, 0, cat == 5 || cat == 9 ? -12.0f : -18.0f);
                setFxValue(factoryProcessor, instance, 1, 3.0f + variantFloat * 0.7f);
                setFxValue(factoryProcessor, instance, 2, 12.0f);
                setFxValue(factoryProcessor, instance, 3, 110.0f);
                setFxValue(factoryProcessor, instance, 4, 6.0f);
                setFxValue(factoryProcessor, instance, 5, 1.0f);
                setFxValue(factoryProcessor, instance, 6, 70.0f);
                setFxValue(factoryProcessor, instance, 7, 85.0f);
                break;
            case FxType::delay:
                setFxValue(factoryProcessor, instance, 0, variant % 2 ? 1.0f : 0.0f);
                setFxValue(factoryProcessor, instance, 1, 0.0f);
                setFxValue(factoryProcessor, instance, 2, 180.0f + variantFloat * 65.0f);
                setFxValue(factoryProcessor, instance, 3, 2.0f);
                setFxValue(factoryProcessor, instance, 4, 24.0f + variantFloat * 8.0f);
                setFxValue(factoryProcessor, instance, 5, 120.0f);
                setFxValue(factoryProcessor, instance, 6, 12000.0f);
                setFxValue(factoryProcessor, instance, 7, 24.0f);
                setFxValue(factoryProcessor, instance, 8, 130.0f);
                break;
        }
    }
    (void) factoryProcessor.setFxChainConfiguration(chain);

    // A small selection of presets have purposeful performance controls.
    if (rowIndex % 5 == 0 && chain.count > 0)
    {
        FxMacroAssignmentSet macros;
        macros.count = 2;
        for (std::size_t assignmentIndex = 0; assignmentIndex < macros.count; ++assignmentIndex)
        {
            const auto parameterIndex = assignmentIndex;
            const auto parameterID = fxParameterID(chain.order[0].type, chain.order[0].slot,
                legacyFxParameter(chain.order[0].type, parameterIndex).toRawUTF8());
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(
                factoryProcessor.getValueTreeState().getParameter(parameterID));
            if (ranged == nullptr) { macros.count = 0; break; }
            const auto range = ranged->getNormalisableRange();
            macros.assignments[assignmentIndex] = {
                assignmentIndex == 0 ? MacroAxis::x : MacroAxis::y,
                chain.order[0].stableIdentity, parameterID, range.start, range.end };
        }
        (void) factoryProcessor.setFxMacroAssignments(macros);
    }
    if (rowIndex % 4 == 1 && chain.count > 0)
    {
        LfoConfiguration lfos;
        lfos.moduleCount = 1;
        auto& lfo = lfos.modules[0]; lfo.stableIdentity = 20000u + static_cast<std::uint64_t>(rowIndex);
        lfo.enabled = true; lfo.waveform = rowIndex == 49 ? dsp::modulation::LfoWaveform::custom : dsp::modulation::LfoWaveform::sine;
        lfo.rateHz = 0.18f + 0.09f * static_cast<float>(variant);
        lfo.depth = 0.35f; lfo.phase = 0.0f; lfo.offset = 0.0f;
        if (lfo.waveform == dsp::modulation::LfoWaveform::custom)
            for (std::size_t i = 0; i < lfo.customWaveform.size(); ++i)
                lfo.customWaveform[i] = 0.65f * std::sin(2.0f * juce::MathConstants<float>::pi
                    * static_cast<float>(i) / static_cast<float>(lfo.customWaveform.size()))
                    + 0.2f * std::sin(4.0f * juce::MathConstants<float>::pi
                    * static_cast<float>(i) / static_cast<float>(lfo.customWaveform.size()));
        lfos.assignments[0] = { lfo.stableIdentity, chain.order[0].stableIdentity,
            fxParameterID(chain.order[0].type, chain.order[0].slot,
                          legacyFxParameter(chain.order[0].type, 1).toRawUTF8()), 0.16f, true, false };
        lfos.assignmentCount = 1;
        (void) factoryProcessor.setLfoConfiguration(lfos);
    }
    factoryProcessor.getStateInformation(cached);
    return cached;
}
}
