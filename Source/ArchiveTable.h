// MIDIARCHIV – the archive table (sort, move and resize columns, inline editing) and the search mask below it.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Columns.h"
#include "Theme.h"
#include <map>
#include <set>

namespace ma
{
class ArchiveTable;

// One search field per visible column, kept aligned with the table header (order, width, scroll).
class SearchFooter : public juce::Component, private juce::TableHeaderComponent::Listener, private juce::ScrollBar::Listener,
                     private juce::Timer
{
public:
    explicit SearchFooter (ArchiveTable& t);
    ~SearchFooter() override;
    void resized() override { layout(); }
    void paint (juce::Graphics&) override;
    void layout();
    void clearAll();
    void setFilter (int col, const juce::String& text);
    std::vector<std::pair<int, juce::String>> filters() const;
    bool hasFilters() const;
    void applyLanguage();

private:
    ArchiveTable& table;
    std::map<int, std::unique_ptr<juce::TextEditor>> fields;
    juce::TextEditor& field (int col);
    void applyTexts (int col, juce::TextEditor&);
    void tableColumnsChanged (juce::TableHeaderComponent*) override { layout(); }
    void tableColumnsResized (juce::TableHeaderComponent*) override { layout(); }
    void tableSortOrderChanged (juce::TableHeaderComponent*) override {}
    void scrollBarMoved (juce::ScrollBar*, double) override { layout(); }
    void timerCallback() override;
};

class ArchiveTable : public juce::Component, public juce::TableListBoxModel, private juce::TextEditor::Listener, private juce::Timer
{
public:
    ArchiveTable (Archive& a, std::function<const Theme&()> themeFn);
    ~ArchiveTable() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    void refresh (bool keepSelection = true);      // filter + sort again
    void repaintRows() { table.repaint(); }
    juce::TableListBox& list() { return table; }
    int numRows() const { return (int) rows.size(); }
    Entry* rowEntry (int row) const { return row >= 0 && row < (int) rows.size() ? rows[(size_t) row] : nullptr; }
    Entry* selectedEntry() const { return rowEntry (table.getSelectedRow()); }
    std::vector<Entry*> selectedEntries() const;
    void selectEntry (int id, bool scroll = true);
    juce::StringArray selectedPaths() const;

    juce::String globalSearch;
    bool onlyDuplicates = false, onlyFavourites = false;
    std::unique_ptr<SearchFooter> footer;   // created after the table; placed by the editor

    std::function<void (Entry*)> onSelect;
    std::function<void (Entry*)> onPlay;
    std::function<void (Entry&, int col, juce::Point<int> screen)> onMenu;
    std::function<void (const juce::String&)> onStatus;

    juce::String saveLayout() const { return table.getHeader().toString(); }
    void restoreLayout (const juce::String& s);
    void applyLanguage();
    void startEdit (int row, int col, const juce::String& typed = {});
    int cursorColumn() const { return curCol; }
    juce::TextEditor* cellEditor() const { return editor.get(); }   // tests
    bool isEditing() const { return editor != nullptr; }
    bool commitValue (Entry& e, int col, const juce::String& value);   // writes into the file (or DB)

    // TableListBoxModel
    int getNumRows() override { return (int) rows.size(); }
    void paintRowBackground (juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell (juce::Graphics&, int row, int col, int w, int h, bool selected) override;
    void sortOrderChanged (int col, bool forwards) override;
    void cellClicked (int row, int col, const juce::MouseEvent&) override;
    void cellDoubleClicked (int row, int col, const juce::MouseEvent&) override;
    void selectedRowsChanged (int lastRow) override;
    void returnKeyPressed (int row) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
    juce::String getCellTooltip (int row, int col) override;

    bool keyPressed (const juce::KeyPress&) override;

private:
    Archive& arch;
    std::function<const Theme&()> theme;
    juce::TableListBox table { "archive", this };
    std::vector<Entry*> rows;
    int sortCol = cId;
    bool sortForward = true;
    std::unique_ptr<juce::TextEditor> editor;
    int editId = 0, editCol = 0;
    int curCol = cTitle;                    // column of the cell cursor (arrow keys)
    bool isEditableCol (int col) const;
    int nextCol (int col, int dir, bool editableOnly) const;
    int rowOfId (int id) const;
    void scrollToColumn (int col);
    void goTo (int row, int col, bool edit);
    bool editorKey (const juce::KeyPress&);
    void paintCellContent (juce::Graphics&, int row, int col, int w, int h);

    void textEditorReturnKeyPressed (juce::TextEditor&) override;
    void textEditorEscapeKeyPressed (juce::TextEditor&) override;
    void textEditorFocusLost (juce::TextEditor&) override;
    void finishEdit (bool commit, bool toSelection = true, bool writeDerived = true);
    void applyValue (const std::vector<int>& ids, int col, const juce::String& value);
    int pendingId = 0, pendingCol = 0;   // single click on an editable cell: editor opens after mouse-up (unless a drag started)
    void timerCallback() override;
    void drawProfile (juce::Graphics&, const Entry&, juce::Rectangle<float>);
};
} // namespace ma
