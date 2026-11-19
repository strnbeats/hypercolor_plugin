#pragma once

#include <JuceHeader.h>
#include <vector>

class HypercolorAudioProcessor;

namespace hypercolor::plugin
{
struct PresetInfo
{
    juce::String id;
    juce::String name;
    juce::String category;
    juce::String description;
    bool factory = false;
    bool favorite = false;
};

class PresetManager
{
public:
    explicit PresetManager(HypercolorAudioProcessor& processor,
                           juce::File userDirectory = {});

    [[nodiscard]] std::vector<PresetInfo> getPresets() const;
    [[nodiscard]] juce::StringArray getCategories() const;
    [[nodiscard]] bool load(const juce::String& id);
    [[nodiscard]] bool saveAs(const juce::String& name,
                              const juce::String& category,
                              const juce::String& description = {});
    [[nodiscard]] bool overwrite(const juce::String& id);
    [[nodiscard]] bool rename(const juce::String& id, const juce::String& newName);
    [[nodiscard]] bool erase(const juce::String& id);
    [[nodiscard]] bool setFavorite(const juce::String& id, bool favorite);
    [[nodiscard]] bool isModified() const;
    [[nodiscard]] juce::String currentId() const { return loadedId; }
    [[nodiscard]] juce::String currentName() const { return loadedName; }
    [[nodiscard]] juce::String currentCategory() const { return loadedCategory; }
    [[nodiscard]] bool currentIsFactory() const noexcept { return loadedFactory; }
    [[nodiscard]] juce::File getUserDirectory() const { return userRoot; }

    static const juce::StringArray& factoryCategories();
    static std::size_t factoryCount() noexcept;

private:
    struct FactoryDefinition;
    [[nodiscard]] juce::MemoryBlock makeFactoryState(const juce::String& id) const;
    [[nodiscard]] juce::File fileForId(const juce::String& id) const;
    [[nodiscard]] bool readUserPreset(const juce::String& id,
                                      PresetInfo& info,
                                      juce::MemoryBlock& state) const;
    [[nodiscard]] bool writeUserPreset(const PresetInfo& info,
                                       const juce::MemoryBlock& state) const;
    void loadFavorites();
    void saveFavorites() const;
    void rememberLoaded(const PresetInfo& info, const juce::MemoryBlock& state);
    [[nodiscard]] juce::MemoryBlock captureCurrentState() const;
    [[nodiscard]] bool applyState(const juce::MemoryBlock& state);
    [[nodiscard]] static bool isSafeName(const juce::String& name);

    HypercolorAudioProcessor& processor;
    juce::File userRoot;
    juce::File favoritesFile;
    juce::StringArray favoriteIds;
    juce::String loadedId;
    juce::String loadedName { "Init" };
    juce::String loadedCategory;
    bool loadedFactory = false;
    juce::MemoryBlock loadedState;
};
}
