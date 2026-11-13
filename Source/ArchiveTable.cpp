#include "ArchiveTable.h"
#include "Look.h"

namespace ma
{
using ui::font;

// ============================================================================================ footer
SearchFooter::SearchFooter (ArchiveTable& t) : table (t)
{
    table.list().getHeader().addListener (this);
    table.list().getHorizontalScrollBar().addListener (this);
}

SearchFooter::~SearchFooter()
{
    table.list().getHeader().removeListener (this);
    table.list().getHorizontalScrollBar().removeListener (this);
}

juce::TextEditor& SearchFooter::field (int col)
{
    auto& f = fields[col];
    if (f == nullptr)
    {
        f = std::make_unique<juce::TextEditor>();
        f->setFont (font (12.5f));
        f->setIndents (4, 3);
        applyTexts (col, *f);
        f->onTextChange = [this] { startTimer (180); };
        f->setEnabled (col != cProfile);
        addAndMakeVisible (*f);
    }
    return *f;
}

void SearchFooter::applyTexts (int col, juce::TextEditor& f)
{
    f.setTextToShowWhenEmpty (col == cProfile ? juce::String() : T ("Suche …"), juce::Colours::grey.withAlpha (0.5f));
    f.setTooltip (T ("Suche in „%1“: Wörter (alle müssen vorkommen), !wort = enthält nicht, = leer, ! nicht leer, =text genau", colName (col))
                  + (colDef (col).numeric ? T (", Zahlen: >120  <=3:00  100-130") : juce::String()));
    f.repaint();
}

void SearchFooter::applyLanguage()
{
    for (auto& [id, f] : fields) applyTexts (id, *f);
}

void SearchFooter::layout()
{
    auto& header = table.list().getHeader();
    std::set<int> visible;
    for (int i = 0; i < header.getNumColumns (true); ++i)
    {
        const int id = header.getColumnIdOfIndex (i, true);
        auto r = getLocalArea (&header, header.getColumnPosition (i));
        auto& f = field (id);
        f.setBounds (r.getX() + 1, 3, juce::jmax (4, r.getWidth() - 2), getHeight() - 6);
        f.setVisible (true);
        visible.insert (id);
    }
    for (auto& [id, f] : fields) if (! visible.count (id)) f->setVisible (false);
}

void SearchFooter::paint (juce::Graphics& g)
{
    g.fillAll (findColour (juce::ResizableWindow::backgroundColourId).brighter (0.04f));
}

void SearchFooter::timerCallback()
{
    stopTimer();
    table.refresh();
}

void SearchFooter::clearAll()
{
    for (auto& [id, f] : fields) f->setText ({}, false);
    table.refresh();
}

void SearchFooter::setFilter (int col, const juce::String& text)
{
    field (col).setText (text, false);
    layout();
    table.refresh();
}

std::vector<std::pair<int, juce::String>> SearchFooter::filters() const
{
    std::vector<std::pair<int, juce::String>> out;
    for (auto& [id, f] : fields)
        if (f->getText().trim().isNotEmpty() && table.list().getHeader().isColumnVisible (id)) out.push_back ({ id, f->getText() });
    return out;
}

bool SearchFooter::hasFilters() const { return ! filters().empty(); }

// ============================================================================================ table
ArchiveTable::ArchiveTable (Archive& a, std::function<const Theme&()> t) : arch (a), theme (std::move (t))
{
    auto& h = table.getHeader();
    for (auto* d = columnDefs(); d->id != 0; ++d)
        h.addColumn (T (d->name), d->id, d->width, 28, 1200, juce::TableHeaderComponent::defaultFlags);
    h.setSortColumnId (cId, true);
    h.setStretchToFitActive (false);
    table.setHeaderHeight (26);
    table.setRowHeight (22);
    table.setMultipleSelectionEnabled (true);
    table.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    table.getViewport()->setScrollBarThickness (12);
    addAndMakeVisible (table);
    footer = std::make_unique<SearchFooter> (*this);
    setWantsKeyboardFocus (false);
}

ArchiveTable::~ArchiveTable()
{
    footer.reset();
    editor.reset();
}

void ArchiveTable::applyLanguage()
{
    auto& h = table.getHeader();
    for (auto* d = columnDefs(); d->id != 0; ++d) h.setColumnName (d->id, T (d->name));
    footer->applyLanguage();
    refresh();
    table.repaint();
}

void ArchiveTable::restoreLayout (const juce::String& s)
{
    if (s.isNotEmpty()) table.getHeader().restoreFromString (s);
    // columns added in later versions must stay reachable
    sortCol = table.getHeader().getSortColumnId();
    sortForward = table.getHeader().isSortedForwards();
    if (sortCol == 0) sortCol = cId;
    footer->layout();
}

void ArchiveTable::resized() { table.setBounds (getLocalBounds()); }

void ArchiveTable::paint (juce::Graphics& g) { g.fillAll (theme().panel); }

void ArchiveTable::refresh (bool keepSelection)
{
    std::vector<int> selected;
    if (keepSelection) for (auto* e : selectedEntries()) selected.push_back (e->id);
    const int anchorId = selectedEntry() != nullptr ? selectedEntry()->id : 0;

    const auto filters = footer->filters();
    juce::StringArray words;
    words.addTokens (globalSearch, " ", "\"");
    words.removeEmptyStrings();
    rows.clear();
    rows.reserve (arch.entries().size());
    for (auto& up : arch.entries())
    {
        auto& e = *up;
        if (onlyDuplicates && e.group == 0) continue;
        if (onlyFavourites && ! e.favourite) continue;
        bool ok = true;
        for (auto& [col, f] : filters) if (! matchFilter (e, col, f)) { ok = false; break; }
        if (ok && ! words.isEmpty())
        {
            const auto hay = e.name + " " + e.title + " " + e.artist + " " + e.composer + " " + e.genre + " " + e.copyright + " " + e.comment + " " + e.folder;
            for (auto& w : words) if (! hay.containsIgnoreCase (w.unquoted())) { ok = false; break; }
        }
        if (ok) rows.push_back (&e);
    }
    const bool numeric = colDef (sortCol).numeric;
    const int col = sortCol;
    const bool fwd = sortForward;
    std::stable_sort (rows.begin(), rows.end(), [=] (const Entry* a, const Entry* b) {
        int c;
        if (numeric)
        {
            const double x = cellNumber (*a, col), y = cellNumber (*b, col);
            c = x < y ? -1 : (x > y ? 1 : 0);
        }
        else
        {
            const auto x = cellText (*a, col), y = cellText (*b, col);
            if (x.isEmpty() != y.isEmpty()) return x.isNotEmpty();   // empty cells always last
            c = x.compareNatural (y, false);
        }
        if (c == 0) c = a->id < b->id ? -1 : 1;
        return fwd ? c < 0 : c > 0;
    });
    table.updateContent();
    if (keepSelection && ! selected.empty())
    {
        std::unordered_map<int, int> rowOf;
        for (int i = 0; i < (int) rows.size(); ++i) rowOf[rows[(size_t) i]->id] = i;
        juce::SparseSet<int> set;
        for (int id : selected) if (auto it = rowOf.find (id); it != rowOf.end()) set.addRange ({ it->second, it->second + 1 });
        table.setSelectedRows (set, juce::dontSendNotification);
        juce::ignoreUnused (anchorId);
    }
    table.repaint();
    if (onStatus) onStatus ({});
}

std::vector<Entry*> ArchiveTable::selectedEntries() const
{
    std::vector<Entry*> out;
    const auto sel = table.getSelectedRows();
    for (int i = 0; i < sel.size(); ++i) if (auto* e = rowEntry (sel[i])) out.push_back (e);
    return out;
}

juce::StringArray ArchiveTable::selectedPaths() const
{
    juce::StringArray p;
    for (auto* e : selectedEntries()) if (! e->has (fMissing)) p.add (e->path);
    return p;
}

void ArchiveTable::selectEntry (int id, bool scroll)
{
    for (int i = 0; i < (int) rows.size(); ++i)
        if (rows[(size_t) i]->id == id)
        {
            table.selectRow (i, ! scroll, true);
            return;
        }
}

void ArchiveTable::paintRowBackground (juce::Graphics& g, int row, int w, int h, bool selected)
{
    const auto& th = theme();
    auto c = (row & 1) ? th.panel : th.panel.interpolatedWith (th.panel2, 0.35f);
    if (selected) c = th.accent.withAlpha (0.38f).withMultipliedSaturation (0.9f);
    g.setColour (c);
    g.fillRect (0, 0, w, h);
    if (auto* e = rowEntry (row); e != nullptr && e->group != 0 && ! selected)
    {
        g.setColour ((e->dupKind == dupSimilar ? th.track[1] : th.track[0]).withAlpha (0.09f));
        g.fillRect (0, 0, w, h);
    }
}

void ArchiveTable::drawProfile (juce::Graphics& g, const Entry& e, juce::Rectangle<float> r)
{
    const auto& th = theme();
    const float bw = r.getWidth() / (float) kMountBins;
    // stacked areas: draw from the top slot down so lower layers stay visible
    for (int t = kMountTracks - 1; t >= 0; --t)
    {
        juce::Path p;
        p.startNewSubPath (r.getX(), r.getBottom());
        for (int b = 0; b < kMountBins; ++b)
        {
            float s = 0;
            for (int k = 0; k <= t; ++k) s += e.mountain[(size_t) (b * kMountTracks + k)];
            p.lineTo (r.getX() + (b + 0.5f) * bw, r.getBottom() - juce::jmin (1.0f, s / 255.0f) * r.getHeight());
        }
        p.lineTo (r.getRight(), r.getBottom());
        p.closeSubPath();
        g.setColour (th.track[(size_t) t].withAlpha (0.85f));
        g.fillPath (p);
    }
}

void ArchiveTable::paintCell (juce::Graphics& g, int row, int col, int w, int h, bool selected)
{
    paintCellContent (g, row, col, w, h);
    // cell cursor (moved with the arrow keys)
    if (selected && col == curCol && row == table.getLastRowSelected())
    {
        g.setColour (theme().accent.withAlpha (table.hasKeyboardFocus (true) || editor != nullptr ? 1.0f : 0.5f));
        g.drawRect (0, 0, w, h, 2);
    }
}

void ArchiveTable::paintCellContent (juce::Graphics& g, int row, int col, int w, int h)
{
    auto* e = rowEntry (row);
    if (e == nullptr) return;
    const auto& th = theme();
    auto r = juce::Rectangle<int> (0, 0, w, h);
    if (col == cProfile) { drawProfile (g, *e, r.reduced (3, 2).toFloat()); return; }
    if (col == cRating)
    {
        const float sw = juce::jmin ((float) h - 6.0f, (float) w / 5.0f - 1.0f);
        for (int i = 0; i < 5; ++i)
        {
            juce::Path star;
            star.addStar ({ 4.0f + sw * (i + 0.5f), h * 0.5f }, 5, sw * 0.22f, sw * 0.5f);
            g.setColour (i < e->rating ? th.track[2] : th.dim.withAlpha (0.25f));
            g.fillPath (star);
        }
        return;
    }
    if (col == cFav)
    {
        juce::Path heart;
        const float s = (float) h * 0.55f, cx = (float) w * 0.5f, cy = (float) h * 0.5f;
        heart.startNewSubPath (cx, cy + s * 0.45f);
        heart.cubicTo (cx - s * 0.9f, cy - s * 0.1f, cx - s * 0.45f, cy - s * 0.75f, cx, cy - s * 0.25f);
        heart.cubicTo (cx + s * 0.45f, cy - s * 0.75f, cx + s * 0.9f, cy - s * 0.1f, cx, cy + s * 0.45f);
        g.setColour (e->favourite ? th.track[0] : th.dim.withAlpha (0.22f));
        g.fillPath (heart);
        return;
    }
    auto text = cellText (*e, col);
    bool derived = (col == cTitle && e->has (fTitleFromName)) || (col == cArtist && e->has (fArtistFromName));
    auto colour = th.text;
    if (derived || e->has (fMissing)) colour = th.dim;
    if (e->has (fError) && col == cComment) colour = th.track[0];
    if (col == cDup && e->group != 0) colour = e->dupKind == dupSimilar ? th.track[1] : th.track[0];
    if (col == cKaraoke && text.isNotEmpty()) text = T ("ja (%1)", text);
    g.setColour (colour);
    g.setFont (derived ? font (13.0f).italicised() : font (13.0f));
    const auto just = colDef (col).numeric && col != cDup ? juce::Justification::centredRight : juce::Justification::centredLeft;
    g.drawText (text.replaceCharacters ("\n\r", "  "), r.reduced (5, 0), just, true);
    if (e->has (fMissing) && col == cFile)
    {
        g.setColour (th.dim);
        g.drawHorizontalLine (h / 2, 4.0f, juce::jmin ((float) w - 4.0f, 6.0f + juce::GlyphArrangement::getStringWidth (font (13.0f), text)));
    }
}

void ArchiveTable::sortOrderChanged (int col, bool forwards)
{
    sortCol = col;
    sortForward = forwards;
    refresh();
}

void ArchiveTable::cellClicked (int row, int col, const juce::MouseEvent& e)
{
    auto* en = rowEntry (row);
    if (en == nullptr) return;
    if (curCol != col) { curCol = col; table.repaint(); }
    if (e.mods.isPopupMenu())
    {
        if (onMenu) onMenu (*en, col, e.getScreenPosition());
        return;
    }
    if (col == cFav) { Archive::UndoGroup g (arch, T ("Favorit: %1", en->name)); arch.setFavourite (*en, ! en->favourite); table.repaintRow (row); }
    else if (col == cRating)
    {
        const int idx = table.getHeader().getIndexOfColumnId (col, true);
        const auto cell = table.getHeader().getColumnPosition (idx);
        const float sw = juce::jmin ((float) table.getRowHeight() - 6.0f, (float) cell.getWidth() / 5.0f - 1.0f);
        const int star = juce::jlimit (0, 5, (int) ((e.x - cell.getX() - 4) / sw) + 1);
        Archive::UndoGroup g (arch, T ("Bewertung: %1", en->name));
        arch.setRating (*en, star == en->rating ? star - 1 : star);
        table.repaintRow (row);
    }
    else if (colDef (col).field >= 0 && ! en->has (fMissing))
    {
        // the last clicked row of a (multi) selection gets the editor directly – selection stays as it is
        pendingId = en->id;
        pendingCol = col;
        startTimer (30);
    }
}

void ArchiveTable::timerCallback()
{
    if (juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
    {
        if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor (this); dc != nullptr && dc->isDragAndDropActive())
            { stopTimer(); pendingId = 0; }
        return;   // wait for mouse-up
    }
    stopTimer();
    const int id = pendingId, col = pendingCol;
    pendingId = 0;
    if (id == 0 || (editor != nullptr && editId == id && editCol == col)) return;
    for (int i = 0; i < (int) rows.size(); ++i)
        if (rows[(size_t) i]->id == id)
        {
            if (table.isRowSelected (i)) startEdit (i, col);
            return;
        }
}

void ArchiveTable::cellDoubleClicked (int row, int col, const juce::MouseEvent&)
{
    if (colDef (col).field < 0 && onPlay) onPlay (rowEntry (row));
}

void ArchiveTable::returnKeyPressed (int row)
{
    // Return on an editable cell opens it (content selected); on the heart it toggles; elsewhere it plays
    auto* e = rowEntry (row);
    if (e == nullptr) return;
    if (isEditableCol (curCol) && ! e->has (fMissing)) { startEdit (row, curCol); return; }
    if (curCol == cFav) { Archive::UndoGroup g (arch, T ("Favorit: %1", e->name)); arch.setFavourite (*e, ! e->favourite); table.repaintRow (row); return; }
    if (onPlay) onPlay (e);
}

bool ArchiveTable::isEditableCol (int col) const
{
    return col != 0 && colDef (col).field >= 0 && table.getHeader().isColumnVisible (col);
}

int ArchiveTable::nextCol (int col, int dir, bool editableOnly) const
{
    auto& h = table.getHeader();
    const int n = h.getNumColumns (true);
    int idx = h.getIndexOfColumnId (col, true);
    if (idx < 0) idx = dir > 0 ? -1 : n;
    for (int i = idx + dir; i >= 0 && i < n; i += dir)
    {
        const int c = h.getColumnIdOfIndex (i, true);
        if (! editableOnly || isEditableCol (c)) return c;
    }
    return col;
}

void ArchiveTable::scrollToColumn (int col)
{
    auto& h = table.getHeader();
    const int idx = h.getIndexOfColumnId (col, true);
    if (idx < 0) return;
    auto* vp = table.getViewport();
    const auto r = h.getColumnPosition (idx);
    int x = vp->getViewPositionX();
    const int w = vp->getViewWidth();
    if (r.getX() < x) x = r.getX();
    else if (r.getRight() > x + w) x = juce::jmin (r.getX(), r.getRight() - w);
    if (x != vp->getViewPositionX()) vp->setViewPosition (x, vp->getViewPositionY());
}

int ArchiveTable::rowOfId (int id) const
{
    for (int i = 0; i < (int) rows.size(); ++i) if (rows[(size_t) i]->id == id) return i;
    return -1;
}

// moves the cell cursor; with edit = true the target cell opens with its content selected
void ArchiveTable::goTo (int row, int col, bool edit)
{
    if (rows.empty()) return;
    row = juce::jlimit (0, (int) rows.size() - 1, row);
    curCol = col;
    if (table.getNumSelectedRows() != 1 || table.getSelectedRow() != row) table.selectRow (row);
    table.scrollToEnsureRowIsOnscreen (row);
    scrollToColumn (col);
    table.repaint();
    if (edit && isEditableCol (col) && ! rows[(size_t) row]->has (fMissing)) startEdit (row, col);
    else table.grabKeyboardFocus();
}

bool ArchiveTable::editorKey (const juce::KeyPress& k)
{
    if (editor == nullptr) return false;
    const int row = rowOfId (editId);
    if (row < 0) return false;
    const int col = editCol;
    const auto code = k.getKeyCode();
    const bool plain = ! k.getModifiers().isCommandDown() && ! k.getModifiers().isAltDown();
    auto commitAndGo = [this] (int toRow, int toCol, bool confirm = false)
    {
        finishEdit (true, false, confirm);
        goTo (toRow, toCol, true);
    };
    if (code == juce::KeyPress::returnKey && plain)
    {
        const auto sel = selectedEntries();
        const int id = editId;
        const bool multi = sel.size() > 1 && std::any_of (sel.begin(), sel.end(), [id] (Entry* x) { return x->id == id; });
        if (multi) { finishEdit (true); return true; }                       // asks before writing into all selected files
        if (row + 1 >= (int) rows.size()) { finishEdit (true, false); goTo (row, col, false); return true; }
        commitAndGo (row + 1, col, true);   // Return confirms (also a title taken from the file name)
        return true;
    }
    if (code == juce::KeyPress::escapeKey) { textEditorEscapeKeyPressed (*editor); return true; }   // at once (TextEditor would post it)
    if (code == juce::KeyPress::tabKey)
    {
        const int c = nextCol (col, k.getModifiers().isShiftDown() ? -1 : 1, true);
        if (c != col) commitAndGo (row, c);
        return true;
    }
    // arrow keys stay inside the open field (caret to start/end); only Return, Tab or Esc leave it
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        const bool shift = k.getModifiers().isShiftDown();
        if (code == juce::KeyPress::upKey) editor->moveCaretToTop (shift); else editor->moveCaretToEnd (shift);
        return true;
    }
    return false;
}

void ArchiveTable::selectedRowsChanged (int)
{
    if (editor) finishEdit (true, false, false);
    if (onSelect) onSelect (selectedEntry());
}

juce::var ArchiveTable::getDragSourceDescription (const juce::SparseSet<int>& sel)
{
    if (sel.isEmpty() || editor != nullptr) return {};
    return "midiarchiv-files";
}

juce::String ArchiveTable::getCellTooltip (int row, int col)
{
    auto* e = rowEntry (row);
    if (e == nullptr) return {};
    if (col == cKaraoke && e->lyrics.isNotEmpty()) return e->lyrics.substring (0, 600);
    if (col == cTitle && e->has (fTitleFromName)) return T ("Aus dem Dateinamen abgeleitet – Klick zum Bearbeiten, Enter schreibt ihn in die Datei");
    if (col == cKey) return T ("Tonart aus den Noten (Sicherheit %1 %)", juce::roundToInt (e->keyConfidence * 100))
                            + (e->keyMeta.isNotEmpty() ? T ("; in der Datei eingetragen: %1", localKey (e->keyMeta)) : juce::String());
    if (col == cDup && e->group != 0)
        if (auto* o = arch.byId (e->bestMatch)) return T ("Ähnlichste Datei: %1", o->name) + "\n" + o->folder;
    auto t = cellText (*e, col);
    if (t.length() > 20 || col == cComment) return t;
    return colTip (col);
}

bool ArchiveTable::keyPressed (const juce::KeyPress& k)
{
    const int row = table.getLastRowSelected();
    if (k.getKeyCode() == juce::KeyPress::F2Key) { startEdit (row, isEditableCol (curCol) ? curCol : cTitle); return true; }
    if (editor != nullptr || row < 0 || rowEntry (row) == nullptr) return false;
    const auto code = k.getKeyCode();
    const auto mods = k.getModifiers();
    if ((code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey) && ! mods.isAnyModifierKeyDown())
    {
        goTo (row, nextCol (curCol, code == juce::KeyPress::leftKey ? -1 : 1, false), false);
        return true;
    }
    if (code == juce::KeyPress::tabKey && ! mods.isCommandDown() && ! mods.isAltDown())
    {
        goTo (row, nextCol (curCol, mods.isShiftDown() ? -1 : 1, true), false);
        return true;
    }
    // typing on an editable cell starts editing with that character (space stays play/stop)
    const auto ch = k.getTextCharacter();
    if (ch > ' ' && ! mods.isCommandDown() && ! mods.isAltDown() && isEditableCol (curCol) && ! rowEntry (row)->has (fMissing))
    {
        if (curCol == cYear && ! juce::CharacterFunctions::isDigit (ch)) return true;
        startEdit (row, curCol, juce::String::charToString (ch));
        return true;
    }
    return false;
}

// ---- inline editing
namespace
{
    // cell editor: arrow keys / Tab / Return go to the table first (cell navigation)
    struct CellEditor : juce::TextEditor
    {
        std::function<bool (const juce::KeyPress&)> onKey;
        bool keyPressed (const juce::KeyPress& k) override { return (onKey && onKey (k)) || juce::TextEditor::keyPressed (k); }
    };
}

void ArchiveTable::startEdit (int row, int col, const juce::String& typed)
{
    auto* e = rowEntry (row);
    if (e == nullptr || colDef (col).field < 0 || ! table.getHeader().isColumnVisible (col)) return;
    if (e->has (fMissing)) { if (onStatus) onStatus (T ("Datei fehlt – nicht bearbeitbar")); return; }
    finishEdit (true, false, false);
    table.scrollToEnsureRowIsOnscreen (row);
    editId = e->id; editCol = col; curCol = col;
    scrollToColumn (col);
    auto ce = std::make_unique<CellEditor>();
    ce->onKey = [this] (const juce::KeyPress& k) { return editorKey (k); };
    editor = std::move (ce);
    editor->setFont (font (13.0f));
    editor->setIndents (4, 3);
    editor->setText (col == cYear ? (e->year > 0 ? juce::String (e->year) : juce::String())
                     : (col == cComment && e->has (fError)) ? juce::String() : cellText (*e, col), false);
    if (col == cYear) editor->setInputRestrictions (4, "0123456789");
    editor->addListener (this);
    const auto& th = theme();
    editor->setColour (juce::TextEditor::outlineColourId, th.accent);
    editor->setColour (juce::TextEditor::focusedOutlineColourId, th.accent);
    editor->setColour (juce::TextEditor::backgroundColourId, th.bg);
    editor->setColour (juce::TextEditor::textColourId, th.text);
    editor->setColour (juce::TextEditor::highlightColourId, th.accent);   // clearly visible selection (solid accent)
    editor->setColour (juce::TextEditor::highlightedTextColourId, th.bg.getPerceivedBrightness() < 0.5f ? juce::Colours::black : juce::Colours::white);
    editor->setColour (juce::CaretComponent::caretColourId, th.text);
    editor->setSelectAllWhenFocused (true);
    editor->applyColourToAllText (th.text);
    // the editor lives in the scrolled row area (not on top of the table): it moves with the mouse wheel / scrollbars
    // and is clipped at the header instead of floating over other rows
    auto* content = table.getViewport()->getViewedComponent();
    editor->setAlwaysOnTop (true);   // stays above row components the list creates while scrolling
    content->addAndMakeVisible (*editor);
    editor->setBounds (content->getLocalArea (&table, table.getCellPosition (col, row, true)).expanded (0, 1));
    editor->grabKeyboardFocus();
    table.repaint();
    if (typed.isNotEmpty())   // typing started the edit: replace the content with that character
    {
        editor->setText (typed, false);
        editor->moveCaretToEnd();
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::TextEditor> (editor.get())] {
            if (safe != nullptr && ! safe->hasKeyboardFocus (false)) { safe->grabKeyboardFocus(); safe->moveCaretToEnd(); }
        });
        return;
    }
    editor->selectAll();
    // focus may arrive a moment later (after the mouse-up) – select again then, caret behind the text
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::TextEditor> (editor.get())] {
        if (safe != nullptr) { if (! safe->hasKeyboardFocus (false)) safe->grabKeyboardFocus(); safe->selectAll(); }
    });
}

void ArchiveTable::textEditorReturnKeyPressed (juce::TextEditor&) { finishEdit (true); }
void ArchiveTable::textEditorEscapeKeyPressed (juce::TextEditor&)
{
    finishEdit (false);
    table.grabKeyboardFocus();   // cell cursor stays where it is
    table.repaint();
}
void ArchiveTable::textEditorFocusLost (juce::TextEditor&) { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ArchiveTable> (this)] { if (safe) safe->finishEdit (true, true, false); }); }

void ArchiveTable::finishEdit (bool commit, bool toSelection, bool writeDerived)
{
    if (editor == nullptr) return;
    auto ed = std::move (editor);
    ed->removeListener (this);
    const auto value = ed->getText().trim();
    const int id = editId, col = editCol;
    ed.reset();
    if (! commit) return;
    auto* e = arch.byId (id);
    if (e == nullptr) return;
    auto needsWrite = [&] (const Entry& x)
    {
        const bool derived = (col == cTitle && x.has (fTitleFromName)) || (col == cArtist && x.has (fArtistFromName));
        const auto current = col == cYear ? (x.year > 0 ? juce::String (x.year) : juce::String()) : cellText (x, col);
        return (derived && writeDerived) || value != current.trim();   // arrow keys only pass by: no write unless changed
    };
    std::vector<int> ids;
    const auto sel = selectedEntries();
    const bool multi = toSelection && sel.size() > 1 && std::find (sel.begin(), sel.end(), e) != sel.end();
    for (auto* x : multi ? sel : std::vector<Entry*> { e })
        if (! x->has (fMissing) && needsWrite (*x)) ids.push_back (x->id);
    if (ids.empty()) return;
    if (ids.size() == 1) { if (auto* x = arch.byId (ids[0])) commitValue (*x, col, value); return; }
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, T ("Mehrere Dateien ändern"),
        T ("%1 = „%2“\n\nin %3 markierte Dateien schreiben?", colName (col), value, (int) ids.size()),
        T ("Alle %1 ändern", (int) ids.size()), T ("Abbrechen"), this,
        juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<ArchiveTable> (this), ids, col, value] (int r) {
            if (r != 0 && safe != nullptr) safe->applyValue (ids, col, value);
        }));
}

void ArchiveTable::applyValue (const std::vector<int>& ids, int col, const juce::String& value)
{
    const int field = colDef (col).field;
    int ok = 0;
    juce::String errors;
    Archive::UndoGroup g (arch, T ("%1 in %2 Dateien", colName (col), (int) ids.size()));
    for (int id : ids)
        if (auto* x = arch.byId (id))
        {
            juce::String err;
            if (arch.setField (*x, (Field) field, field == (int) Field::keySignature ? keyInputToGerman (value) : value, err)) ++ok;
            else errors << x->name << ": " << err << "\n";
        }
    if (errors.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Midi-Fundus", T ("Nicht geschrieben:") + "\n" + errors.substring (0, 2000));
    if (onStatus) onStatus (T ("%1 in %2 Dateien geschrieben (Originale einmalig gesichert)", colName (col), ok));
    table.repaint();
}

bool ArchiveTable::commitValue (Entry& e, int col, const juce::String& value)
{
    const int field = colDef (col).field;
    if (field < 0) return false;
    juce::String err;
    Archive::UndoGroup g (arch, colName (col) + ": " + e.name);
    if (! arch.setField (e, (Field) field, field == (int) Field::keySignature ? keyInputToGerman (value) : value, err))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Midi-Fundus",
                                                T ("Konnte nicht in die Datei schreiben:") + "\n" + e.path + "\n\n" + err);
        return false;
    }
    if (onStatus) onStatus (T ("Gespeichert in %1 (Original einmalig gesichert in %APPDATA%\\MIDIARCHIV\\Sicherung)", e.name));
    table.repaint();
    return true;
}
} // namespace ma
