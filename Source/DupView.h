// MIDIARCHIV – duplicate panel: group members, note mountains, 12-tone profile and an aligned piano-roll overlay (green = same, red = different).
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Archive.h"
#include "Theme.h"
#include "Lang.h"

namespace ma
{
struct Comparison
{
    MidiInfo a, b;
    int transpose = 0;          // add to B's pitches to match A
    float offset = 0;           // add to B's start (beats) to match A
    std::vector<bool> aMatched, bMatched;
    float aShare = 0, bShare = 0;
    bool valid = false;
};

Comparison compareFiles (const juce::File& a, const juce::File& b);

// Draws the stacked "mountain" of an entry (also used by the table)
void drawMountain (juce::Graphics& g, const Entry& e, juce::Rectangle<float> r, const Theme& th, bool outline);

class DupView : public juce::Component, private juce::ListBoxModel
{
public:
    DupView (Archive& a, std::function<const Theme&()> themeFn);
    void setEntry (Entry* e);           // shows the group of e (A = e)
    void refreshList();
    void applyLanguage();
    int currentA() const { return idA; }
    int currentB() const { return idB; }

    std::function<void (int id)> onSelectInTable;
    std::function<void (Entry*)> onPlay;
    std::function<void (const juce::String&)> onStatus;
    std::function<void (int id, std::function<void()> after)> onTrash;   // confirm + Papierkorb (editor)

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    Archive& arch;
    std::function<const Theme&()> theme;
    juce::ListBox members { "members", this };
    juce::TextButton playA { juce::String::fromUTF8 ("▶ A") }, playB { juce::String::fromUTF8 ("▶ B") }, showB { "B in Tabelle" },
        notDup { "Kein Duplikat" }, trashA { "A in Papierkorb" }, trashB { "B in Papierkorb" };
    std::vector<int> ids;
    int idA = 0, idB = 0;
    Comparison cmp;
    float zoom = 1.0f, scroll = 0.0f, dragStartScroll = 0.0f;
    juce::Rectangle<int> rollArea, mountA, mountB, pcArea, infoArea;

    void choose (int idOfB);
    int getNumRows() override { return (int) ids.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void drawRoll (juce::Graphics&, juce::Rectangle<int>);
    void drawPitchClasses (juce::Graphics&, juce::Rectangle<int>);
};
} // namespace ma
