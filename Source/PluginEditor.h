#pragma once
#include "PluginProcessor.h"
#include "Archive.h"
#include "ArchiveTable.h"
#include "DupView.h"
#include "WebLookup.h"
#include "Look.h"

class MidiArchivEditor;

namespace ma::ui
{
class AboutPanel : public juce::Component
{
public:
    explicit AboutPanel (MidiArchivEditor& e);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void showPage (int page);
    static constexpr const char* kWebsiteUrl = "https://www.undabtanzbar.de";
    static constexpr const char* kDonateUrl = "https://paypal.me/yakobo";
private:
    MidiArchivEditor& ed;
    juce::TextEditor text;
    juce::String about, tips;
    juce::TextButton tabAbout, tabTips { "Tooltipps" }, close, donate { "Spenden (PayPal)" }, website { "undabtanzbar.de" };
    juce::Rectangle<int> box() const;
};

// Suggestions to write into one file (online lookup, deep mode), plus web-search shortcuts.
class SuggestPanel : public juce::Component
{
public:
    struct Row { Field field; juce::String value, source; };
    SuggestPanel (MidiArchivEditor& e, int entryId, juce::String heading);
    void setBusy (const juce::String& message);
    void setResults (const std::vector<Row>& rows, const juce::String& log);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    int entryId() const { return id; }
private:
    MidiArchivEditor& ed;
    int id;
    juce::String heading, message;
    std::vector<Row> rows;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::Component content;
    juce::Viewport view;
    juce::TextEditor log;
    juce::OwnedArray<juce::TextButton> engines;
    juce::TextButton apply { juce::String::fromUTF8 ("Übernehmen") }, close { juce::String::fromUTF8 ("Schließen") };
    juce::Rectangle<int> box() const;
};

// Folder window: which folders to read (after adding folders: the new ones) and which are write-protected.
class FolderPickPanel : public juce::Component
{
public:
    FolderPickPanel (MidiArchivEditor& e, const juce::StringArray& newFolders);   // empty = "manage folders" from the menu
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
private:
    MidiArchivEditor& ed;
    juce::StringArray paths;
    juce::OwnedArray<juce::ToggleButton> toggles, locks;
    juce::Component content;
    void updateStart();
    juce::Viewport view;
    juce::TextButton all, none, start, cancel;
    juce::Rectangle<int> box() const;
    void closeLater();
};

class PositionBar : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit PositionBar (MidiArchivEditor& e) : ed (e) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent& e) override { mouseDown (e); }
private:
    MidiArchivEditor& ed;
};

class DragBar : public juce::Component
{
public:
    std::function<void (int dy)> onDrag;
    std::function<void()> onEnd;
    DragBar() { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); }
    void paint (juce::Graphics& g) override;
    void mouseDrag (const juce::MouseEvent& e) override { if (onDrag) onDrag (e.getDistanceFromDragStartY()); }
    void mouseUp (const juce::MouseEvent&) override { if (onEnd) onEnd(); }
};
} // namespace ma::ui

class MidiArchivEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, public juce::FileDragAndDropTarget,
                         private juce::Timer
{
public:
    explicit MidiArchivEditor (MidiArchivProcessor&);
    ~MidiArchivEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override;
    bool shouldDropFilesWhenDraggedExternally (const juce::DragAndDropTarget::SourceDetails&, juce::StringArray& files, bool& canMove) override;

    const ma::Theme& theme() const { return look.theme(); }
    void applyTheme();
    void applyLanguage();
    ma::Archive& archive() { return *arch; }
    MidiArchivProcessor& processor() { return proc; }
    ma::ArchiveTable& tableView() { return table; }
    void setStatus (const juce::String& s);
    void showAbout (bool show);
    void closeSuggest();
    void closeFolderPick();
    void confirmRemove (const juce::String& title, const juce::String& text, std::function<int (ma::Archive&)> action);
    void scanAddedFolders (const juce::StringArray& added, bool manage = false);   // new folders + a choice of the existing ones; manage: from the menu
    void play (ma::Entry* e);
    void showCellMenu (ma::Entry& e, int col, juce::Point<int> screen);
    void startOnlineLookup (ma::Entry& e);
    void startMelodySearch (ma::Entry& e);
    void undo();
    void copyMelody (ma::Entry& e, bool openMusipedia);
    std::vector<int> targetsFor (ma::Entry& clicked);              // all marked rows if the clicked one is marked
    void trashFiles (const std::vector<int>& allIds, std::function<void()> after = {});
    void copyOrMoveFiles (const std::vector<int>& ids, bool move);
    void convertFiles (const std::vector<int>& ids, int format);   // ExportFormat: 0 MIDI, 1 MusicXML, 2 ABC, 3 DAWproject, 4 REAPER
    void refreshNow();
    juce::ThreadPool& jobs() { return pool; }

private:
    MidiArchivProcessor& proc;
    juce::SharedResourcePointer<ma::Archive> arch;
    juce::SharedResourcePointer<juce::TooltipWindow> tooltip;
    ma::ui::Look look;
    ma::ArchiveTable table;
    ma::DupView dups;
    ma::ui::DragBar dragBar;
    ma::ui::PositionBar position { *this };
    juce::TextButton folders { "Ordner" }, scan { "Einlesen" }, playBtn { juce::String::fromUTF8 ("▶") }, stopBtn { juce::String::fromUTF8 ("■") },
        settingsBtn { juce::String::fromUTF8 ("Einstellungen") }, aboutBtn { "?" }, clearFilters { juce::String::fromUTF8 ("Filter löschen") };
    juce::ToggleButton onlyDup { "Nur Dubletten" }, onlyFav { "Nur Favoriten" }, dupPanel { "Dubletten-Ansicht" };
    juce::TextEditor search;
    juce::Label emptyHint;
    juce::Rectangle<int> logoArea, statusArea, headerArea;
    std::unique_ptr<ma::ui::AboutPanel> about;
    std::unique_ptr<ma::ui::FolderPickPanel> folderPick;
    std::unique_ptr<ma::ui::SuggestPanel> suggest;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ThreadPool pool { 2 };
    juce::String statusText;
    juce::uint32 shownVersion = 0, lastRefresh = 0;
    int dupHeight = 300, dragStartHeight = 300;
    bool firstRun = true;
    bool sizeRestored = false, viewRestored = false;
    int pendingSelect = 0, viewTicks = 0;
    juce::uint32 pendingSince = 0;
    juce::String lastViewXml;
    void saveViewState();
    void restoreViewState();

    void timerCallback() override;
    void showFolderMenu();
    void showSettingsMenu();
    void addFolderDialog();
    void layoutChanged();
    void updateDupPanel();
};
