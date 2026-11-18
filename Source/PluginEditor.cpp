#include "PluginEditor.h"
#include "IconArt.h"
#include "UltraStar.h"
#include "Formats.h"
#include "Export.h"

using namespace ma;
using ma::ui::font;
static juce::String U (const char* s) { return juce::String::fromUTF8 (s); }

static juce::String fieldName (Field f)
{
    switch (f)
    {
        case Field::title: return T ("Titel");
        case Field::artist: return T ("Interpret");
        case Field::composer: return T ("Komponist");
        case Field::genre: return T ("Genre");
        case Field::year: return T ("Jahr");
        case Field::copyright: return T ("Copyright");
        case Field::comment: return T ("Kommentar");
        case Field::language: return T ("Sprache");
        case Field::keySignature: return T ("Tonart");
    }
    return {};
}

static juce::String currentValue (const Entry& e, Field f)
{
    switch (f)
    {
        case Field::title: return e.has (fTitleFromName) ? juce::String() : e.title;
        case Field::artist: return e.has (fArtistFromName) ? juce::String() : e.artist;
        case Field::composer: return e.composer;
        case Field::genre: return e.genre;
        case Field::year: return e.year > 0 ? juce::String (e.year) : juce::String();
        case Field::copyright: return e.copyright;
        case Field::comment: return e.comment;
        case Field::language: return e.language;
        case Field::keySignature: return localKey (e.keyMeta);
    }
    return {};
}

// ============================================================================================ About
namespace ma::ui
{
AboutPanel::AboutPanel (MidiArchivEditor& e) : ed (e)
{
    tabAbout.setButtonText (T ("Über Midi-Fundus"));
    tabTips.setButtonText (T ("Tooltipps"));
    close.setButtonText (T ("Schließen"));
    donate.setButtonText (T ("Spenden (PayPal)"));
    text.setMultiLine (true);
    text.setReadOnly (true);
    text.setScrollbarsShown (true);
    text.setCaretVisible (false);
    text.setFont (font (14.0f));
    text.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    text.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    about = T ("@about");
    tips = T ("@tips");
    for (auto* b : { &tabAbout, &tabTips })
    {
        b->setRadioGroupId (404);
        b->setClickingTogglesState (true);
        addAndMakeVisible (b);
    }
    tabAbout.onClick = [this] { showPage (0); };
    tabTips.onClick = [this] { showPage (1); };
    donate.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffd6336c));
    donate.setTooltip (kDonateUrl);
    website.setTooltip (kWebsiteUrl);
    donate.onClick = [] { juce::URL (kDonateUrl).launchInDefaultBrowser(); };
    website.onClick = [] { juce::URL (kWebsiteUrl).launchInDefaultBrowser(); };
    close.onClick = [this] { ed.showAbout (false); };
    for (auto* c : std::initializer_list<juce::Component*> { &donate, &website, &close, &text }) addAndMakeVisible (c);
    showPage (0);
}

juce::Rectangle<int> AboutPanel::box() const { return getLocalBounds().withSizeKeepingCentre (juce::jmin (860, getWidth() - 40), juce::jmin (720, getHeight() - 40)); }

void AboutPanel::showPage (int page)
{
    (page == 0 ? tabAbout : tabTips).setToggleState (true, juce::dontSendNotification);
    text.setText (page == 0 ? about : tips, false);
    text.moveCaretToTop (false);
}

void AboutPanel::mouseDown (const juce::MouseEvent& e) { if (! box().contains (e.getPosition())) ed.showAbout (false); }

void AboutPanel::paint (juce::Graphics& g)
{
    const auto& th = ed.theme();
    g.fillAll (th.bg.withAlpha (0.75f));
    auto b = box().toFloat();
    g.setColour (th.panel);
    g.fillRoundedRectangle (b, 12.0f);
    g.setColour (th.accent);
    g.drawRoundedRectangle (b, 12.0f, 1.5f);
    auto head = box().reduced (18).removeFromTop (72);
    drawLogo (g, head.removeFromLeft (72).toFloat());
    head.removeFromLeft (14);
    g.setColour (th.text);
    g.setFont (font (26.0f, true));
    g.drawText ("Midi-Fundus", head.removeFromTop (40), juce::Justification::bottomLeft);
    g.setColour (th.dim);
    g.setFont (font (14.0f));
    g.drawText (T ("Version %1  ·  MIDI-Archiv mit Dublettenfinder  ·  von Un(d)AbtanzBAR", JucePlugin_VersionString), head, juce::Justification::topLeft);
}

void AboutPanel::resized()
{
    auto b = box().reduced (18);
    b.removeFromTop (82);
    auto tabs = b.removeFromTop (30);
    tabAbout.setBounds (tabs.removeFromLeft (180));
    tabs.removeFromLeft (6);
    tabTips.setBounds (tabs.removeFromLeft (130));
    b.removeFromTop (8);
    auto bottom = b.removeFromBottom (32);
    close.setBounds (bottom.removeFromRight (110));
    donate.setBounds (bottom.removeFromLeft (210));
    bottom.removeFromLeft (8);
    website.setBounds (bottom.removeFromLeft (170));
    b.removeFromBottom (10);
    text.setBounds (b);
}

// ============================================================================================ suggestions
SuggestPanel::SuggestPanel (MidiArchivEditor& e, int entryId, juce::String h) : ed (e), id (entryId), heading (std::move (h))
{
    view.setViewedComponent (&content, false);
    view.setScrollBarsShown (true, false);
    addAndMakeVisible (view);
    log.setMultiLine (true);
    log.setReadOnly (true);
    log.setFont (font (12.0f));
    addAndMakeVisible (log);
    auto* entry = ed.archive().byId (id);
    juce::String q = entry != nullptr ? cleanTitleForSearch (entry->title + " " + entry->artist) : juce::String();
    for (int i = 0; i < kNumEngines; ++i)
    {
        auto* b = engines.add (new juce::TextButton (juce::String::fromUTF8 (kEngineNames[i]).upToFirstOccurrenceOf (" ", false, false)));
        b->setTooltip (T ("Im Browser suchen: %1", q));
        b->onClick = [i, q] { webSearchUrl (i, q).launchInDefaultBrowser(); };
        addAndMakeVisible (b);
    }
    apply.setButtonText (T ("Übernehmen"));
    close.setButtonText (T ("Schließen"));
    apply.setEnabled (false);
    apply.onClick = [this] {
        auto* en = ed.archive().byId (id);
        if (en == nullptr) return;
        int written = 0;
        juce::String errors;
        Archive::UndoGroup g (ed.archive(), T ("Online-Angaben: %1", en->name));
        for (int i = 0; i < toggles.size(); ++i)
            if (toggles[i]->getToggleState())
            {
                juce::String err;
                if (ed.archive().setField (*en, rows[(size_t) i].field, rows[(size_t) i].value, err)) ++written;
                else errors << fieldName (rows[(size_t) i].field) << ": " << err << "\n";
            }
        ed.setStatus (T ("%1 Angabe(n) in „%2“ geschrieben", written, en->name));
        if (errors.isNotEmpty()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Midi-Fundus", errors);
        ed.refreshNow();
        ed.closeSuggest();
    };
    close.onClick = [this] { ed.closeSuggest(); };
    addAndMakeVisible (apply);
    addAndMakeVisible (close);
}

juce::Rectangle<int> SuggestPanel::box() const { return getLocalBounds().withSizeKeepingCentre (juce::jmin (900, getWidth() - 40), juce::jmin (600, getHeight() - 40)); }

void SuggestPanel::setBusy (const juce::String& m) { message = m; repaint(); }

void SuggestPanel::setResults (const std::vector<Row>& r, const juce::String& text)
{
    rows = r;
    message = rows.empty() ? T ("Nichts Passendes gefunden. Die Knöpfe unten öffnen die Suche im Browser.") : T ("Haken setzen und „Übernehmen“ – die Werte werden in die Datei geschrieben.");
    log.setText (text, false);
    toggles.clear();
    auto* en = ed.archive().byId (id);
    std::set<int> taken;
    for (auto& row : rows)
    {
        const auto cur = en != nullptr ? currentValue (*en, row.field) : juce::String();
        auto* t = toggles.add (new juce::ToggleButton (fieldName (row.field) + ":  " + row.value + U ("      ← ") + row.source
                                                       + (cur.isNotEmpty() ? T ("   (bisher: %1)", cur) : juce::String())));
        const bool on = cur.isEmpty() && ! taken.count ((int) row.field) && ! row.value.equalsIgnoreCase (cur);
        if (on) taken.insert ((int) row.field);
        t->setToggleState (on, juce::dontSendNotification);
        const int idx = toggles.size() - 1;
        t->onClick = [this, idx] {
            if (toggles[idx]->getToggleState())
                for (int k = 0; k < toggles.size(); ++k)
                    if (k != idx && rows[(size_t) k].field == rows[(size_t) idx].field) toggles[k]->setToggleState (false, juce::dontSendNotification);
        };
        content.addAndMakeVisible (t);
    }
    apply.setEnabled (! rows.empty());
    resized();
    repaint();
}

void SuggestPanel::mouseDown (const juce::MouseEvent& e) { if (! box().contains (e.getPosition())) ed.closeSuggest(); }

void SuggestPanel::paint (juce::Graphics& g)
{
    const auto& th = ed.theme();
    g.fillAll (th.bg.withAlpha (0.7f));
    auto b = box().toFloat();
    g.setColour (th.panel);
    g.fillRoundedRectangle (b, 12.0f);
    g.setColour (th.accent);
    g.drawRoundedRectangle (b, 12.0f, 1.5f);
    auto r = box().reduced (18);
    g.setColour (th.text);
    g.setFont (font (17.0f, true));
    g.drawText (heading, r.removeFromTop (26), juce::Justification::centredLeft, true);
    g.setColour (th.dim);
    g.setFont (font (13.0f));
    g.drawText (message, r.removeFromTop (22), juce::Justification::centredLeft, true);
}

void SuggestPanel::resized()
{
    auto r = box().reduced (18);
    r.removeFromTop (54);
    auto bottom = r.removeFromBottom (30);
    close.setBounds (bottom.removeFromRight (110));
    bottom.removeFromRight (8);
    apply.setBounds (bottom.removeFromRight (130));
    bottom.removeFromRight (16);
    const int ew = juce::jmin (100, (bottom.getWidth() - 4 * kNumEngines) / juce::jmax (1, kNumEngines));
    for (auto* b : engines) { b->setBounds (bottom.removeFromLeft (ew)); bottom.removeFromLeft (4); }
    r.removeFromBottom (10);
    log.setBounds (r.removeFromBottom (juce::jmin (110, r.getHeight() / 3)));
    r.removeFromBottom (8);
    view.setBounds (r);
    content.setSize (r.getWidth() - 14, juce::jmax (r.getHeight(), toggles.size() * 28));
    for (int i = 0; i < toggles.size(); ++i) toggles[i]->setBounds (0, i * 28, content.getWidth(), 26);
}

// ============================================================================================ folder picker
FolderPickPanel::FolderPickPanel (MidiArchivEditor& e, const juce::StringArray& newFolders) : ed (e)
{
    view.setViewedComponent (&content, false);
    view.setScrollBarsShown (true, false);
    addAndMakeVisible (view);
    auto& arch = ed.archive();
    std::vector<int> counts ((size_t) arch.folders.size(), 0);
    for (auto& en : arch.entries())
        for (int i = 0; i < arch.folders.size(); ++i)
            if (en->path.startsWithIgnoreCase (arch.folders[i] + juce::File::getSeparatorString())) { ++counts[(size_t) i]; break; }
    const auto prot = arch.protectedFolders();
    // new folders first (ticked for reading), then the existing ones
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < arch.folders.size(); ++i)
        {
            const auto& f = arch.folders[i];
            const bool isNew = newFolders.contains (f, true);
            if (isNew != (pass == 0)) continue;
            paths.add (f);
            auto* t = toggles.add (new juce::ToggleButton (f + "   " + (isNew ? T ("(neu)") : T ("(%1 Dateien im Archiv)", counts[(size_t) i]))));
            t->setToggleState (isNew, juce::dontSendNotification);
            t->onClick = [this] { updateStart(); };
            content.addAndMakeVisible (t);
            auto* l = locks.add (new juce::ToggleButton (T ("schreibgeschützt")));
            l->setToggleState (prot.contains (f, true), juce::dontSendNotification);
            l->setTooltip (T ("Midi-Fundus schreibt, verschiebt und löscht in diesem Ordner nichts (Dateien erscheinen rötlich)"));
            content.addAndMakeVisible (l);
        }
    all.setButtonText (T ("Alle"));
    none.setButtonText (T ("Keine"));
    cancel.setButtonText (T ("Abbrechen"));
    all.setTooltip (T ("Alle Ordner einlesen"));
    none.setTooltip (T ("Keinen Ordner einlesen"));
    all.onClick = [this] { for (auto* t : toggles) t->setToggleState (true, juce::dontSendNotification); updateStart(); };
    none.onClick = [this] { for (auto* t : toggles) t->setToggleState (false, juce::dontSendNotification); updateStart(); };
    start.onClick = [this] {
        auto& a = ed.archive();
        juce::StringArray chosen;
        for (int i = 0; i < toggles.size(); ++i)
        {
            if (locks[i]->getToggleState() != a.protectedFolders().contains (paths[i], true)) a.setProtected (paths[i], locks[i]->getToggleState());
            if (toggles[i]->getToggleState()) chosen.add (paths[i]);
        }
        if (! chosen.isEmpty()) a.startScan (false, chosen);
        ed.refreshNow();
        closeLater();
    };
    cancel.onClick = [this] { closeLater(); };
    for (auto* b : { &all, &none, &start, &cancel }) addAndMakeVisible (b);
    updateStart();
}

void FolderPickPanel::updateStart()
{
    bool any = false;
    for (auto* t : toggles) any |= t->getToggleState();
    start.setButtonText (any ? T ("Einlesen") : T ("Übernehmen"));
}

void FolderPickPanel::closeLater()   // not from inside the button callback that would delete itself
{
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MidiArchivEditor> (&ed)] { if (safe != nullptr) safe->closeFolderPick(); });
}

juce::Rectangle<int> FolderPickPanel::box() const
{
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (940, getWidth() - 40), juce::jmin (196 + toggles.size() * 28, juce::jmin (600, getHeight() - 40)));
}

void FolderPickPanel::mouseDown (const juce::MouseEvent& e) { if (! box().contains (e.getPosition())) ed.closeFolderPick(); }

void FolderPickPanel::paint (juce::Graphics& g)
{
    const auto& th = ed.theme();
    g.fillAll (th.bg.withAlpha (0.7f));
    auto b = box().toFloat();
    g.setColour (th.panel);
    g.fillRoundedRectangle (b, 12.0f);
    g.setColour (th.accent);
    g.drawRoundedRectangle (b, 12.0f, 1.5f);
    auto r = box().reduced (18);
    g.setColour (th.text);
    g.setFont (font (17.0f, true));
    g.drawText (T ("Ordner einlesen und schützen"), r.removeFromTop (26), juce::Justification::centredLeft, true);
    g.setColour (th.dim);
    g.setFont (font (13.0f));
    g.drawFittedText (T ("Links: welche Ordner jetzt eingelesen werden (neue sind angehakt, nicht angehakte bleiben, wie sie sind). Rechts: schreibgeschützte Ordner – dort ändert, verschiebt und löscht Midi-Fundus nichts."),
                      r.removeFromTop (40), juce::Justification::centredLeft, 2);
    r.removeFromTop (6);
    auto head = r.removeFromTop (18);
    g.setFont (font (12.0f, true));
    g.drawText (T ("Einlesen"), head, juce::Justification::centredLeft);
    g.drawText (T ("Schreibschutz"), head.removeFromRight (190), juce::Justification::centredLeft);
}

void FolderPickPanel::resized()
{
    auto r = box().reduced (18);
    r.removeFromTop (98);
    auto bottom = r.removeFromBottom (30);
    cancel.setBounds (bottom.removeFromRight (110));
    bottom.removeFromRight (8);
    start.setBounds (bottom.removeFromRight (130));
    all.setBounds (bottom.removeFromLeft (80));
    bottom.removeFromLeft (6);
    none.setBounds (bottom.removeFromLeft (80));
    r.removeFromBottom (10);
    view.setBounds (r);
    content.setSize (r.getWidth() - 14, juce::jmax (r.getHeight(), toggles.size() * 28));
    const int lockW = 176;
    for (int i = 0; i < toggles.size(); ++i)
    {
        toggles[i]->setBounds (0, i * 28, content.getWidth() - lockW - 8, 26);
        locks[i]->setBounds (content.getWidth() - lockW, i * 28, lockW, 26);
    }
}

// ============================================================================================ small parts
void PositionBar::paint (juce::Graphics& g)
{
    const auto& th = ed.theme();
    auto r = getLocalBounds().toFloat();
    g.setColour (th.panel2);
    g.fillRoundedRectangle (r, 5.0f);
    auto& p = ed.processor();
    const double len = p.length();
    if (len > 0)
    {
        g.setColour (th.accent.withAlpha (p.isPlaying() ? 0.55f : 0.3f));
        g.fillRoundedRectangle (r.withWidth ((float) (r.getWidth() * juce::jlimit (0.0, 1.0, p.position() / len))), 5.0f);
    }
    g.setColour (th.text);
    g.setFont (font (12.0f));
    const auto name = juce::File (p.playingPath()).getFileName();
    g.drawText (name.isEmpty() ? T ("Vorhören: Leertaste / Doppelklick") : name + "   " + formatDuration (p.position()) + " / " + formatDuration (len),
                getLocalBounds().reduced (8, 0), juce::Justification::centredLeft, true);
}

void PositionBar::mouseDown (const juce::MouseEvent& e)
{
    auto& p = ed.processor();
    if (p.length() > 0) p.seek (p.length() * juce::jlimit (0.0, 1.0, e.position.x / (double) getWidth()));
}

void DragBar::paint (juce::Graphics& g)
{
    g.setColour (findColour (juce::ScrollBar::thumbColourId));
    g.fillRoundedRectangle (getLocalBounds().toFloat().withSizeKeepingCentre (60.0f, 3.0f), 1.5f);
}
} // namespace ma::ui

// ============================================================================================ editor
MidiArchivEditor::MidiArchivEditor (MidiArchivProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      table (*arch, [this]() -> const Theme& { return theme(); }),
      dups (*arch, [this]() -> const Theme& { return theme(); })
{
    setLookAndFeel (&look);
    auto& st = arch->settings;
    dupHeight = st.getProperty ("dupHeight", 300);

    addAndMakeVisible (table);
    addAndMakeVisible (*table.footer);
    addAndMakeVisible (dups);
    addAndMakeVisible (dragBar);
    addAndMakeVisible (position);
    for (auto* b : { &folders, &scan, &playBtn, &stopBtn, &settingsBtn, &aboutBtn, &clearFilters }) addAndMakeVisible (b);
    for (auto* b : { &onlyDup, &onlyFav, &dupPanel }) addAndMakeVisible (b);
    addAndMakeVisible (search);
    emptyHint.setJustificationType (juce::Justification::centred);
    emptyHint.setFont (font (16.0f));
    emptyHint.setInterceptsMouseClicks (false, false);
    addChildComponent (emptyHint);

    search.setFont (font (14.0f));
    search.setIndents (8, 5);
    search.onTextChange = [this] { table.globalSearch = search.getText(); table.refresh(); };
    onlyDup.onClick = [this] {
        table.onlyDuplicates = onlyDup.getToggleState();
        if (table.onlyDuplicates) table.list().getHeader().setSortColumnId (cDup, true);
        table.refresh();
    };
    onlyFav.onClick = [this] { table.onlyFavourites = onlyFav.getToggleState(); table.refresh(); };
    dupPanel.setToggleState (st.getProperty ("dupShown", true), juce::dontSendNotification);
    dupPanel.onClick = [this] { arch->settings.setProperty ("dupShown", dupPanel.getToggleState(), nullptr); arch->saveSoon(); resized(); updateDupPanel(); };
    clearFilters.onClick = [this] { search.clear(); table.globalSearch = {}; table.footer->clearAll(); };

    folders.onClick = [this] { showFolderMenu(); };
    scan.onClick = [this] {
        if (arch->isScanning()) arch->stopScan();
        else if (arch->folders.isEmpty()) addFolderDialog();
        else arch->startScan();
    };
    playBtn.onClick = [this] { play (table.selectedEntry()); };
    stopBtn.onClick = [this] { proc.stop(); };
    settingsBtn.onClick = [this] { showSettingsMenu(); };
    aboutBtn.onClick = [this] { showAbout (true); };
    addChildComponent (updateBtn);
    updateBtn.onClick = [this] { showUpdateMenu(); };
    updateBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2f9e44));
    updateBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    refreshUpdateHint();   // cached result of the last check (a new one arrives via the timer)

    loupe = std::make_unique<ma::ui::Loupe>();
    addChildComponent (*loupe);
    catcher = std::make_unique<ma::ui::RightClickCatcher>();
    catcher->onRightClick = [this] { showLoupeMenu(); };
    catcher->passThrough = [this] (juce::Point<int> p) {
        if (inPassThrough) return true;
        if (about != nullptr || folderPick != nullptr || suggest != nullptr) return true;   // overlays
        // table rows have their own menu (it contains "Lupe" too)
        if (table.getBounds().contains (p))
        {
            auto& lb = table.list();
            const auto lp = lb.getLocalPoint (this, p);
            if (lp.y >= lb.getHeaderHeight() && lb.getRowContainingPosition (lp.x, lp.y) >= 0) return true;
        }
        // text fields keep their copy / paste menu
        const juce::ScopedValueSetter<bool> guard (inPassThrough, true);
        for (auto* c = getComponentAt (p); c != nullptr && c != this; c = c->getParentComponent())
            if (dynamic_cast<juce::TextEditor*> (c) != nullptr) return true;
        return false;
    };
    addAndMakeVisible (*catcher);
    catcher->setBounds (getLocalBounds());   // the editor may already have its size (resized() ran before the catcher existed)
    addMouseListener (&loupeTracker, true);

    table.onSelect = [this] (Entry* e) { updateDupPanel(); if (e != nullptr && ! e->has (fMissing)) proc.setCue (e->path); };
    proc.onHostTransport = [this] { setStatus (proc.isPlaying() ? T ("DAW: Play – spielt die markierte Datei ab DAW-Position") : T ("DAW: Stopp")); };
    table.onPlay = [this] (Entry* e) { play (e); };
    table.onMenu = [this] (Entry& e, int col, juce::Point<int> s) { showCellMenu (e, col, s); };
    table.onStatus = [this] (const juce::String& s) { if (s.isNotEmpty()) setStatus (s); else repaint (statusArea); };
    table.restoreLayout (st.getProperty ("columns").toString());

    dups.onSelectInTable = [this] (int id) {
        table.selectEntry (id);
        if (table.selectedEntry() == nullptr || table.selectedEntry()->id != id)
        {
            table.footer->clearAll(); search.clear(); table.globalSearch = {}; onlyDup.setToggleState (false, juce::sendNotification);
            table.selectEntry (id);
        }
    };
    dups.onPlay = [this] (Entry* e) { play (e); };
    dups.onStatus = [this] (const juce::String& s) { setStatus (s); };
    dups.onTrash = [this] (int id, std::function<void()> after) { trashFiles ({ id }, std::move (after)); };

    dragBar.onDrag = [this] (int dy) { dupHeight = juce::jlimit (120, juce::jmax (140, getHeight() - 260), dragStartHeight - dy); resized(); };
    dragBar.onEnd = [this] { dragStartHeight = dupHeight; arch->settings.setProperty ("dupHeight", dupHeight, nullptr); arch->saveSoon(); };
    dragStartHeight = dupHeight;

    applyLanguage();
    applyTheme();
    // read the remembered size first: setResizeLimits() may resize (and resized() would overwrite it)
    const int w = proc.uiState.getProperty ("w", 1440), h = proc.uiState.getProperty ("h", 880);
    setResizable (true, true);
    setResizeLimits (900, 520, 5000, 3200);
    setSize (juce::jlimit (900, 5000, w), juce::jlimit (520, 3200, h));
    sizeRestored = true;
    setWantsKeyboardFocus (true);
    restoreViewState();
    refreshNow();
    if (pendingSelect != 0) table.selectEntry (pendingSelect);
    startTimerHz (10);
}

MidiArchivEditor::~MidiArchivEditor()
{
    proc.onHostTransport = nullptr;
    removeMouseListener (&loupeTracker);
    pool.removeAllJobs (true, 15000);
    saveViewState();
    about.reset();
    suggest.reset();
    folderPick.reset();
    setLookAndFeel (nullptr);
}

void MidiArchivEditor::applyLanguage()
{
    folders.setButtonText (T ("Ordner"));
    settingsBtn.setButtonText (T ("Einstellungen"));
    clearFilters.setButtonText (T ("Filter löschen"));
    scan.setButtonText (arch->isScanning() ? T ("Stopp") : T ("Einlesen"));
    emptyHint.setText (T ("Noch leer. Über „Ordner“ einen oder mehrere Ordner mit MIDI-Dateien hinzufügen (oder ins Fenster ziehen) und „Einlesen“ klicken."), juce::dontSendNotification);
    folders.setTooltip (T ("Ordner hinzufügen oder entfernen (Ordner lassen sich auch ins Fenster ziehen)"));
    scan.setTooltip (T ("Alle Ordner einlesen – neue und geänderte Dateien werden analysiert, Dubletten sofort gesucht"));
    playBtn.setTooltip (T ("Markierte Datei abspielen (Leertaste)"));
    stopBtn.setTooltip (T ("Stopp"));
    settingsBtn.setTooltip (T ("Farbthema, Sprache, Klang, Dubletten-Schwelle, Datenbank"));
    aboutBtn.setTooltip (T ("Über Midi-Fundus und Tooltipps"));
    shownUpdate = "-";   // texts in the new language
    refreshUpdateHint();
    search.setTextToShowWhenEmpty (T ("Suche in allen Feldern …"), juce::Colours::grey);
    search.repaint();
    onlyDup.setTooltip (T ("Nur Dateien zeigen, die zu einer Dubletten-Gruppe gehören"));
    onlyFav.setTooltip (T ("Nur Favoriten zeigen"));
    dupPanel.setTooltip (T ("Dubletten-Ansicht unter der Tabelle ein- oder ausblenden"));
    clearFilters.setTooltip (T ("Alle Suchfelder leeren"));
    table.applyLanguage();
    dups.applyLanguage();
    about.reset();
    suggest.reset();
    folderPick.reset();
    resized();
    refreshNow();
    repaint();
}

void MidiArchivEditor::applyTheme()
{
    const int idx = juce::jlimit (0, (int) themes().size() - 1, (int) arch->settings.getProperty ("theme", 0));
    const bool light = arch->settings.getProperty ("light", false);
    look.setTheme (light ? lightVariant (themes()[(size_t) idx]) : themes()[(size_t) idx]);
    const auto& th = look.theme();
    look.setColour (juce::TableHeaderComponent::backgroundColourId, th.panel2);
    look.setColour (juce::TableHeaderComponent::textColourId, th.text);
    look.setColour (juce::TableHeaderComponent::outlineColourId, th.bg);
    look.setColour (juce::TableHeaderComponent::highlightColourId, th.accent.withAlpha (0.3f));
    look.setColour (juce::ListBox::backgroundColourId, th.panel);
    sendLookAndFeelChange();
    repaint();
}

void MidiArchivEditor::paint (juce::Graphics& g)
{
    const auto& th = theme();
    g.fillAll (th.bg);
    g.setColour (th.panel);
    g.fillRect (headerArea);
    drawLogo (g, logoArea.toFloat().removeFromLeft ((float) logoArea.getHeight()));
    g.setColour (th.text);
    g.setFont (font (20.0f, true));
    g.drawText ("Midi-Fundus", logoArea.withTrimmedLeft (logoArea.getHeight() + 8), juce::Justification::centredLeft);

    // status bar
    g.setColour (th.panel);
    g.fillRect (statusArea);
    g.setColour (th.dim);
    g.setFont (font (12.5f));
    juce::String s;
    s << T ("%1 Dateien  ·  %2 angezeigt  ·  %3 Dubletten-Gruppen", (int) arch->entries().size(), table.numRows(), arch->duplicateGroups());
    if (arch->isScanning()) s << U ("  ·  ") << arch->scanStatus();
    if (statusText.isNotEmpty()) s << U ("  ·  ") << statusText;
    g.drawText (s, statusArea.reduced (10, 0), juce::Justification::centredLeft, true);
    if (arch->isScanning() && arch->scanTotal() > 0)
    {
        g.setColour (th.accent);
        g.fillRect (statusArea.getX(), statusArea.getY(), (int) (statusArea.getWidth() * (double) arch->scanDone() / arch->scanTotal()), 2);
    }
}

void MidiArchivEditor::resized()
{
    if (catcher != nullptr) { catcher->setBounds (getLocalBounds()); catcher->toFront (false); }
    if (loupe != nullptr) loupe->toFront (false);
    auto r = getLocalBounds();
    headerArea = r.removeFromTop (48);
    auto h = headerArea.reduced (10, 8);
    logoArea = h.removeFromLeft (170);
    h.removeFromLeft (6);
    folders.setBounds (h.removeFromLeft (76)); h.removeFromLeft (6);
    scan.setBounds (h.removeFromLeft (92)); h.removeFromLeft (14);
    aboutBtn.setBounds (h.removeFromRight (32)); h.removeFromRight (6);
    settingsBtn.setBounds (h.removeFromRight (110)); h.removeFromRight (14);
    if (updateBtn.isVisible()) { updateBtn.setBounds (h.removeFromRight (juce::jmin (190, h.getWidth() / 5))); h.removeFromRight (10); }
    stopBtn.setBounds (h.removeFromRight (32)); h.removeFromRight (4);
    playBtn.setBounds (h.removeFromRight (32)); h.removeFromRight (6);
    // toggles get their room first, then the player bar, the rest is the search field (min. 140 px)
    const int toggles = 150 + 124 + 124;
    const int posW = juce::jlimit (110, 360, juce::jmin (h.getWidth() / 4, h.getWidth() - toggles - 14 - 8 - 140));
    position.setBounds (h.removeFromRight (posW)); h.removeFromRight (14);
    const bool compact = h.getWidth() < toggles + 8 + 140;
    onlyDup.setButtonText (compact ? T ("Dubl.") : T ("Nur Dubletten"));
    onlyFav.setButtonText (compact ? T ("Fav.") : T ("Nur Favoriten"));
    dupPanel.setButtonText (compact ? T ("Ansicht") : T ("Dubletten-Ansicht"));
    dupPanel.setBounds (h.removeFromRight (compact ? 86 : 150));
    onlyFav.setBounds (h.removeFromRight (compact ? 66 : 124));
    onlyDup.setBounds (h.removeFromRight (compact ? 70 : 124)); h.removeFromRight (8);
    search.setBounds (h);

    statusArea = r.removeFromBottom (24);
    clearFilters.setBounds (statusArea.withLeft (statusArea.getRight() - 150).reduced (2, 2));
    if (dupPanel.getToggleState())
    {
        dupHeight = juce::jlimit (120, juce::jmax (140, r.getHeight() - 200), dupHeight);
        dups.setVisible (true);
        dragBar.setVisible (true);
        dups.setBounds (r.removeFromBottom (dupHeight));
        dragBar.setBounds (r.removeFromBottom (8));
    }
    else { dups.setVisible (false); dragBar.setVisible (false); }
    auto foot = r.removeFromBottom (30);
    table.footer->setBounds (foot);
    table.setBounds (r);
    emptyHint.setBounds (r.reduced (40, 80));
    table.footer->layout();
    if (about) about->setBounds (getLocalBounds());
    if (suggest) suggest->setBounds (getLocalBounds());
    if (folderPick) folderPick->setBounds (getLocalBounds());
    if (sizeRestored)
    {
        proc.uiState.setProperty ("w", getWidth(), nullptr);
        proc.uiState.setProperty ("h", getHeight(), nullptr);
    }
}

void MidiArchivEditor::mouseDown (const juce::MouseEvent& e)
{
    if (logoArea.contains (e.getPosition())) showAbout (true);
}

bool MidiArchivEditor::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::spaceKey)
    {
        if (proc.isPlaying()) proc.stop(); else play (table.selectedEntry());
        return true;
    }
    if (k == juce::KeyPress::escapeKey) { if (folderPick) { closeFolderPick(); return true; } if (about) { showAbout (false); return true; } if (suggest) { closeSuggest(); return true; } proc.stop(); return true; }
    if (k == juce::KeyPress ('f', juce::ModifierKeys::ctrlModifier, 0)) { search.grabKeyboardFocus(); return true; }
    if (k == juce::KeyPress ('z', juce::ModifierKeys::ctrlModifier, 0)) { undo(); return true; }
    // navigation keys nobody else took (focus on a button, the duplicate view, nowhere …): back to the table
    const int code = k.getKeyCode();
    const bool nav = code == juce::KeyPress::upKey || code == juce::KeyPress::downKey || code == juce::KeyPress::pageUpKey
                  || code == juce::KeyPress::pageDownKey || code == juce::KeyPress::homeKey || code == juce::KeyPress::endKey;
    if (! table.isEditing() && (nav || code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey
                                || code == juce::KeyPress::F2Key || code == juce::KeyPress::returnKey))
    {
        table.list().grabKeyboardFocus();
        if (nav && (table.keyPressed (k) || table.list().keyPressed (k))) return true;
        if (code == juce::KeyPress::returnKey) { table.list().keyPressed (k); return true; }
    }
    return table.keyPressed (k);
}

// Column order/widths/sorting, search fields, toggles and the selected file survive a restart.
// Kept in a small view.xml next to the database (the database itself is far too big to rewrite on every filter change).
static juce::File viewFile (ma::Archive& a) { return a.databaseFile().getSiblingFile ("view.xml"); }

void MidiArchivEditor::saveViewState()
{
    if (! viewRestored) return;
    juce::ValueTree v ("VIEW");
    v.setProperty ("columns", table.saveLayout(), nullptr);
    v.setProperty ("search", search.getText(), nullptr);
    v.setProperty ("onlyDup", onlyDup.getToggleState(), nullptr);
    v.setProperty ("onlyFav", onlyFav.getToggleState(), nullptr);
    for (auto& [col, text] : table.footer->filters()) v.setProperty ("f" + juce::String (col), text, nullptr);
    const int sel = table.selectedEntry() != nullptr ? table.selectedEntry()->id : pendingSelect;
    if (sel != 0) v.setProperty ("sel", sel, nullptr);
    const auto xml = v.toXmlString();
    if (xml == lastViewXml) return;
    lastViewXml = xml;
    viewFile (*arch).replaceWithText (xml);
    arch->settings.setProperty ("columns", v.getProperty ("columns"), nullptr);   // older versions read it from here
}

void MidiArchivEditor::restoreViewState()
{
    viewRestored = true;
    auto v = juce::ValueTree::fromXml (viewFile (*arch).loadFileAsString());
    if (! v.isValid() || ! v.hasType ("VIEW")) return;
    lastViewXml = v.toXmlString();
    if (v.hasProperty ("columns")) table.restoreLayout (v.getProperty ("columns").toString());
    search.setText (v.getProperty ("search").toString(), juce::dontSendNotification);
    table.globalSearch = search.getText();
    onlyDup.setToggleState (v.getProperty ("onlyDup", false), juce::dontSendNotification);
    onlyFav.setToggleState (v.getProperty ("onlyFav", false), juce::dontSendNotification);
    table.onlyDuplicates = onlyDup.getToggleState();
    table.onlyFavourites = onlyFav.getToggleState();
    for (int i = 0; i < v.getNumProperties(); ++i)
    {
        const auto name = v.getPropertyName (i).toString();
        if (name.startsWithChar ('f') && name.substring (1).containsOnly ("0123456789") && name.length() > 1)
            table.footer->setFilter (name.substring (1).getIntValue(), v.getProperty (name).toString());
    }
    pendingSelect = v.getProperty ("sel", 0);
    pendingSince = juce::Time::getMillisecondCounter();
}

void MidiArchivEditor::timerCallback()
{
    position.repaint();
    // the remembered file may only appear once the duplicate index is ready ("Nur Dubletten")
    if (pendingSelect != 0)
    {
        if (table.selectedEntry() != nullptr && table.selectedEntry()->id == pendingSelect) pendingSelect = 0;
        else if (juce::Time::getMillisecondCounter() - pendingSince > 20000) pendingSelect = 0;
        else if (arch->byId (pendingSelect) == nullptr) pendingSelect = 0;
        else { table.selectEntry (pendingSelect); if (table.selectedEntry() != nullptr && table.selectedEntry()->id == pendingSelect) pendingSelect = 0; }
    }
    if (++viewTicks >= 20) { viewTicks = 0; saveViewState(); refreshUpdateHint(); }   // every 2 s
    if (loupe != nullptr && (loupe->isVisible() || (bool) proc.uiState.getProperty ("loupeOn", false))) updateLoupe();
    playBtn.setToggleState (proc.isPlaying(), juce::dontSendNotification);
    scan.setButtonText (arch->isScanning() ? T ("Stopp") : T ("Einlesen"));
    const auto now = juce::Time::getMillisecondCounter();
    if (arch->version() != shownVersion && ! table.isEditing()
        && now - lastRefresh > (arch->isScanning() ? 1500u : 250u) && ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        refreshNow();
    if (arch->isScanning()) repaint (statusArea);
    if (firstRun)
    {
        firstRun = false;
        if (arch->entries().empty() && arch->folders.isEmpty()) setStatus (T ("Willkommen! Zuerst Ordner hinzufügen."));
    }
}

void MidiArchivEditor::refreshNow()
{
    emptyHint.setVisible (arch->entries().empty() && ! arch->isScanning());
    emptyHint.toFront (false);
    shownVersion = arch->version();
    lastRefresh = juce::Time::getMillisecondCounter();
    table.refresh();
    dups.refreshList();
    repaint (statusArea);
}

void MidiArchivEditor::setStatus (const juce::String& s)
{
    statusText = s;
    repaint (statusArea);
}

void MidiArchivEditor::updateDupPanel()
{
    if (! dupPanel.getToggleState()) return;
    auto* e = table.selectedEntry();
    if ((e == nullptr ? 0 : e->id) == dups.currentA() && e != nullptr) return;
    dups.setEntry (e);
}

void MidiArchivEditor::play (Entry* e)
{
    if (e == nullptr) return;
    if (ma::hasLockedNotes (e->path)) { proc.stop(); setStatus (ma::lockedNotesMessage()); return; }
    if (! proc.play (juce::File (e->path))) setStatus (T ("Kann nicht abgespielt werden: %1", e->name));
    else setStatus ({});
}

void MidiArchivEditor::showAbout (bool show)
{
    if (show)
    {
        about = std::make_unique<ma::ui::AboutPanel> (*this);
        addAndMakeVisible (*about);
        about->setBounds (getLocalBounds());
    }
    else about.reset();
}

void MidiArchivEditor::closeSuggest() { suggest.reset(); }

// New folders are always read; folders already in the archive only if the user ticks them (walking big folders takes time).
void MidiArchivEditor::scanAddedFolders (const juce::StringArray& added, bool manage)
{
    if (arch->isScanning()) { if (! manage) setStatus (T ("Ordner hinzugefügt – „Einlesen“, sobald der laufende Scan fertig ist")); return; }
    bool others = false;
    for (auto& f : arch->folders) others |= ! added.contains (f, true);
    if (! others && ! manage) { arch->startScan (false, added); return; }
    folderPick = std::make_unique<ma::ui::FolderPickPanel> (*this, added);
    addAndMakeVisible (*folderPick);
    folderPick->setBounds (getLocalBounds());
}

void MidiArchivEditor::closeFolderPick() { folderPick.reset(); }

void MidiArchivEditor::confirmRemove (const juce::String& title, const juce::String& text, std::function<int (Archive&)> action)
{
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, title, text, T ("Entfernen"), T ("Abbrechen"), this,
        juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<MidiArchivEditor> (this), action] (int r) {
            if (r == 0 || safe == nullptr) return;
            safe->proc.stop();
            const int n = action (*safe->arch);
            safe->setStatus (T ("%1 Einträge aus dem Fundus entfernt", n));
            safe->refreshNow();
            safe->dups.setEntry (safe->table.selectedEntry());
        }));
}

void MidiArchivEditor::filesDropped (const juce::StringArray& files, int, int)
{
    juce::StringArray added;
    for (auto& f : files)
    {
        juce::File file (f);
        if (file.isDirectory()) added.addIfNotAlreadyThere (arch->addFolder (file), true);
        else if (isArchiveFile (file)) added.addIfNotAlreadyThere (arch->addFolder (file.getParentDirectory()), true);
    }
    if (! added.isEmpty()) scanAddedFolders (added);
}

bool MidiArchivEditor::shouldDropFilesWhenDraggedExternally (const juce::DragAndDropTarget::SourceDetails& d, juce::StringArray& files, bool& canMove)
{
    if (d.description.toString() != "midiarchiv-files") return false;
    files = table.selectedPaths();
    // other formats go out as a converted MIDI file (temporary copy)
    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Midi-Fundus");
    juce::StringArray out;
    int locked = 0;
    for (auto& p : files)
    {
        if (isMidiFileName (p)) { out.add (p); continue; }
        if (hasLockedNotes (p)) { ++locked; continue; }
        if (auto mid = midiCopyOf (juce::File (p), tmp); mid.existsAsFile()) out.add (mid.getFullPathName());
    }
    if (locked > 0) setStatus (lockedNotesMessage());
    files = out;
    canMove = false;
    return ! files.isEmpty();
}

void MidiArchivEditor::addFolderDialog()
{
    chooser = std::make_unique<juce::FileChooser> (T ("Ordner mit MIDI-Dateien wählen"), juce::File (arch->settings.getProperty ("lastFolder").toString()));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe = juce::Component::SafePointer<MidiArchivEditor> (this)] (const juce::FileChooser& fc) {
                              if (safe == nullptr || fc.getResult() == juce::File()) return;
                              safe->arch->settings.setProperty ("lastFolder", fc.getResult().getFullPathName(), nullptr);
                              safe->scanAddedFolders ({ safe->arch->addFolder (fc.getResult()) });
                          });
}

void MidiArchivEditor::showFolderMenu()
{
    juce::PopupMenu m;
    m.addItem (1, T ("Ordner hinzufügen …"));
    m.addItem (2, T ("Ordner verwalten (einlesen, Schreibschutz) …"), ! arch->folders.isEmpty() && ! arch->isScanning());
    m.addSeparator();
    if (arch->folders.isEmpty()) m.addItem (-1, T ("(noch keine Ordner)"), false);
    for (int i = 0; i < arch->folders.size(); ++i)
    {
        juce::PopupMenu sub;
        sub.addItem (100 + i, T ("Im Explorer öffnen"));
        sub.addItem (300 + i, T ("Aus dem Fundus entfernen (Ordner und Einträge) …"));
        sub.addSeparator();
        const bool prot = arch->protectedFolders().contains (arch->folders[i], true);
        sub.addItem (400 + i, T ("Schreibgeschützt"), true, prot);
        m.addSubMenu (arch->folders[i] + (prot ? "   " + T ("(schreibgeschützt)") : juce::String()), sub);
    }
    if (const int outside = arch->countOutsideFolders(); outside > 0)
    {
        m.addSeparator();
        m.addItem (3, T ("%1 Einträge ohne Ordner entfernen …", outside), ! arch->isScanning());
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (folders), [safe = juce::Component::SafePointer<MidiArchivEditor> (this)] (int r) {
        if (safe == nullptr || r == 0) return;
        auto& a = *safe->arch;
        if (r == 1) safe->addFolderDialog();
        else if (r == 2) safe->scanAddedFolders ({}, true);
        else if (r >= 400 && r < 500) { const auto f = a.folders[r - 400]; a.setProtected (f, ! a.protectedFolders().contains (f, true)); safe->refreshNow(); }
        else if (r >= 100 && r < 200) juce::File (a.folders[r - 100]).startAsProcess();
        else if (r == 3) safe->confirmRemove (T ("Einträge ohne Ordner entfernen?"),
                                              T ("%1 Einträge gehören zu keinem Ordner im Fundus mehr. Sie werden aus der Datenbank entfernt (mit Bewertung und Favorit); die Dateien selbst bleiben unangetastet.", a.countOutsideFolders()),
                                              [] (Archive& x) { return x.removeOutsideFolders(); });
        else if (r >= 300 && r < 400)
        {
            const auto f = a.folders[r - 300];
            safe->confirmRemove (T ("Ordner aus dem Fundus entfernen?"),
                                 T ("„%1“ und seine %2 Einträge werden aus der Datenbank entfernt (mit Bewertung und Favorit). Die Dateien selbst bleiben unangetastet. Ein laufender Scan wird dafür angehalten.", f, a.countInFolder (f)),
                                 [f] (Archive& x) { return x.removeFolder (f); });
        }
    });
}

// ============================================================================================ loupe
void MidiArchivEditor::addLoupeMenu (juce::PopupMenu& m, int id0)
{
    auto& ui = proc.uiState;
    const bool on = ui.getProperty ("loupeOn", false);
    const int zoom = ui.getProperty ("loupeZoom", 250), size = ui.getProperty ("loupeSize", 1);
    m.addItem (id0, on ? T ("Lupe ausschalten") : T ("Lupe einschalten"));
    m.addSeparator();
    m.addSectionHeader (T ("Vergrößerung"));
    for (int i = 0; i < (int) std::size (ma::ui::LoupeSettings::kZooms); ++i)
    {
        const int z = ma::ui::LoupeSettings::kZooms[i];
        auto txt = juce::String (z / 100.0, z % 100 == 0 ? 0 : 1);
        if (language() != Lang::en) txt = txt.replaceCharacter ('.', ',');
        m.addItem (id0 + 2 + i, txt + juce::String::fromUTF8 ("×"), true, zoom == z);
    }
    m.addSeparator();
    m.addSectionHeader (T ("Lupengröße"));
    const juce::String sizes[] = { T ("klein"), T ("mittel"), T ("groß") };
    for (int i = 0; i < 3; ++i) m.addItem (id0 + 10 + i, sizes[i], true, size == i);
}

void MidiArchivEditor::handleLoupeMenu (int r, int id0)
{
    auto ui = proc.uiState;
    if (r == id0) ui.setProperty ("loupeOn", ! (bool) ui.getProperty ("loupeOn", false), nullptr);
    else if (r >= id0 + 2 && r < id0 + 2 + (int) std::size (ma::ui::LoupeSettings::kZooms))
    { ui.setProperty ("loupeZoom", ma::ui::LoupeSettings::kZooms[r - id0 - 2], nullptr); ui.setProperty ("loupeOn", true, nullptr); }
    else if (r >= id0 + 10 && r < id0 + 13) { ui.setProperty ("loupeSize", r - id0 - 10, nullptr); ui.setProperty ("loupeOn", true, nullptr); }
    updateLoupe();
}

void MidiArchivEditor::showLoupeMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader (T ("Lupe"));
    addLoupeMenu (m, 1);
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe = juce::Component::SafePointer<MidiArchivEditor> (this)] (int r) {
        if (safe != nullptr && r != 0) safe->handleLoupeMenu (r, 1);
    });
}

void MidiArchivEditor::showLoupeAt (juce::Point<int> p)
{
    proc.uiState.setProperty ("loupeOn", true, nullptr);
    loupeTestPos = p;
    updateLoupe();
}

void MidiArchivEditor::updateLoupe()
{
    if (loupe == nullptr) return;
    auto& ui = proc.uiState;
    const auto mouse = loupeTestPos.x >= 0 ? loupeTestPos : getMouseXYRelative();
    if (! (bool) ui.getProperty ("loupeOn", false) || ! getLocalBounds().contains (mouse) || (loupeTestPos.x < 0 && ! isShowing()))
    {
        loupe->setVisible (false);
        return;
    }
    const float zoom = (float) (int) ui.getProperty ("loupeZoom", 250) / 100.0f;
    const auto size = ma::ui::LoupeSettings::windowSize ((int) ui.getProperty ("loupeSize", 1));
    const int gap = 90;   // distance from the mouse pointer (user: 48 was too close)
    const juce::Point<int> candidates[] = { { mouse.x + gap, mouse.y + gap }, { mouse.x - gap - size.x, mouse.y + gap },
                                            { mouse.x + gap, mouse.y - gap - size.y }, { mouse.x - gap - size.x, mouse.y - gap - size.y } };
    auto pos = candidates[0];
    for (auto c : candidates)
        if (getLocalBounds().contains (juce::Rectangle<int> (c.x, c.y, size.x, size.y))) { pos = c; break; }
    const auto area = juce::Rectangle<float> ((float) size.x / zoom, (float) size.y / zoom).withCentre (mouse.toFloat()).getSmallestIntegerContainer();
    float display = 1.0f;
    if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds())) display = (float) d->scale;
    loupe->setVisible (false);
    auto img = createComponentSnapshot (area, true, zoom * display);
    loupe->setBounds (juce::jlimit (0, juce::jmax (0, getWidth() - size.x), pos.x),
                      juce::jlimit (0, juce::jmax (0, getHeight() - size.y), pos.y), size.x, size.y);
    loupe->frame = theme().accent;
    loupe->setImage (img);
    loupe->setVisible (true);
    loupe->toFront (false);
}

void MidiArchivEditor::refreshUpdateHint()
{
    const auto v = updateCheck->newerVersion();
    if (v == shownUpdate && updateBtn.isVisible() == v.isNotEmpty()) return;
    shownUpdate = v;
    updateBtn.setButtonText (T ("Update %1 verfügbar", v));
    updateBtn.setTooltip (T ("Midi-Fundus %1 ist erschienen (installiert: %2) – klicken für den Download", v, MA_VERSION_STRING));
    updateBtn.setVisible (v.isNotEmpty());
    resized();
}

void MidiArchivEditor::showUpdateMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader (T ("Midi-Fundus %1 ist erschienen", shownUpdate));
    m.addItem (1, T ("Download-Seite öffnen …"));
    m.addItem (2, T ("Bis zum nächsten Start ausblenden"));
    m.addSeparator();
    m.addItem (3, T ("Nicht mehr nach Updates suchen"));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (updateBtn), [safe = juce::Component::SafePointer<MidiArchivEditor> (this)] (int r) {
        if (safe == nullptr || r == 0) return;
        if (r == 1) juce::URL (ma::UpdateCheck::kReleasesUrl).launchInDefaultBrowser();
        else if (r == 2) safe->updateCheck->dismiss();
        else if (r == 3) safe->updateCheck->setEnabled (false);
        safe->refreshUpdateHint();
    });
}

void MidiArchivEditor::showSettingsMenu()
{
    auto& st = arch->settings;
    juce::PopupMenu m, th, sim;
    const int cur = st.getProperty ("theme", 0);
    for (int i = 0; i < (int) themes().size(); ++i) th.addItem (100 + i, themes()[(size_t) i].name, true, i == cur);
    th.addSeparator();
    th.addItem (150, T ("Hell"), true, (bool) st.getProperty ("light", false));
    m.addSubMenu (T ("Farbthema"), th);
    {
        juce::PopupMenu lang;
        lang.addItem (400, "Deutsch", true, language() == Lang::de);
        lang.addItem (401, "English", true, language() == Lang::en);
        lang.addItem (402, U ("Français"), true, language() == Lang::fr);
        m.addSubMenu (U ("Sprache · Language · Langue"), lang);
    }
    const int thr = juce::roundToInt (arch->similarityThreshold() * 100);
    for (int v : { 25, 30, 35, 40, 45, 50, 60, 70, 80, 90 }) sim.addItem (200 + v, juce::String (v) + " %", true, v == thr);
    m.addSubMenu (T ("Ähnlichkeitsschwelle (Dubletten)"), sim);
    m.addSeparator();
    {
        juce::PopupMenu snd;
        const int md = proc.soundMode();
        auto& sf = proc.soundFont();
        snd.addItem (13, T ("General-MIDI-SoundFont: %1", sf.isLoaded() ? sf.file().getFileNameWithoutExtension() : T ("(nicht geladen)")),
                     sf.isLoaded(), md == MidiArchivProcessor::sndSoundFont);
        snd.addItem (12, T ("General MIDI von Windows (Microsoft GS Wavetable Synth – läuft an der DAW vorbei)"),
                     MidiArchivProcessor::windowsGmAvailable(), md == MidiArchivProcessor::sndWindowsGm);
        snd.addItem (10, T ("Eingebauter Synth-Klang (Kreisel, kein GM)"), true, md == MidiArchivProcessor::sndKreisel);
        snd.addItem (14, T ("Kein eigener Klang"), true, md == MidiArchivProcessor::sndNone);
        snd.addSeparator();
        snd.addItem (15, T ("Anderen SoundFont wählen (.sf2) …"));
        snd.addItem (16, T ("Standard-SoundFont GeneralUser GS verwenden"), sf.file() != SoundFontPlayer::defaultSoundFont());
        m.addSubMenu (T ("Klang beim Vorhören"), snd);
    }
    m.addItem (11, T ("Vorhören zusätzlich als MIDI an die DAW / den MIDI-Ausgang"), true, proc.midiOut.load());
    if (! proc.isStandalone())
    {
        m.addItem (17, T ("Play/Stopp der DAW folgen (Play spielt die markierte Datei)"), true, proc.hostSync());
        m.addItem (18, T ("Tempo der DAW übernehmen (sonst eigenes Tempo der Datei)"), proc.hostSync(), proc.hostTempo());
    }
    m.addSeparator();
    m.addItem (20, T ("Alle Dateien neu einlesen (auch unveränderte)"), ! arch->isScanning());
    m.addItem (21, T ("Fehlende Dateien aus der Datenbank entfernen"));
    m.addItem (22, T ("Spaltenanordnung zurücksetzen"));
    m.addItem (23, T ("Einmal am Tag nach Updates suchen (GitHub)"), true, updateCheck->enabled());
    m.addSeparator();
    m.addItem (30, T ("Datenbank-Ordner öffnen"));
    m.addItem (31, T ("Sicherungsordner (Originale vor Änderungen) öffnen"));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (settingsBtn), [safe = juce::Component::SafePointer<MidiArchivEditor> (this)] (int r) {
        if (safe == nullptr || r == 0) return;
        auto& a = *safe->arch;
        if (r >= 100 && r < 150) { a.settings.setProperty ("theme", r - 100, nullptr); safe->applyTheme(); }
        else if (r >= 400 && r <= 402) { chooseLanguage (a.settings, (Lang) (r - 400)); safe->applyLanguage(); }
        else if (r == 150) { a.settings.setProperty ("light", ! (bool) a.settings.getProperty ("light", false), nullptr); safe->applyTheme(); }
        else if (r >= 200 && r < 300) { a.setSimilarityThreshold ((r - 200) / 100.0f); safe->refreshNow(); safe->dups.setEntry (safe->table.selectedEntry()); }
        else if (r == 10) safe->proc.setSoundMode (MidiArchivProcessor::sndKreisel);
        else if (r == 11) safe->proc.midiOut = ! safe->proc.midiOut.load();
        else if (r == 17) safe->proc.setHostSync (! safe->proc.hostSync());
        else if (r == 23)
        {
            const bool on = ! safe->updateCheck->enabled();
            safe->updateCheck->setEnabled (on);
            safe->setStatus (on ? T ("Update-Suche an – gesendet wird nur die Anfrage an GitHub, keine Daten über dich oder dein Archiv")
                                : T ("Update-Suche aus"));
            safe->refreshUpdateHint();
        }
        else if (r == 18) safe->proc.setHostTempo (! safe->proc.hostTempo());
        else if (r == 12) safe->proc.setSoundMode (MidiArchivProcessor::sndWindowsGm);
        else if (r == 13) safe->proc.setSoundMode (MidiArchivProcessor::sndSoundFont);
        else if (r == 14) safe->proc.setSoundMode (MidiArchivProcessor::sndNone);
        else if (r == 16) { safe->proc.loadSoundFontAsync ({}); safe->proc.setSoundMode (MidiArchivProcessor::sndSoundFont); }
        else if (r == 15)
        {
            safe->chooser = std::make_unique<juce::FileChooser> (T ("SoundFont wählen"), safe->proc.soundFont().file(), "*.sf2");
            safe->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc) {
                if (safe == nullptr || ! fc.getResult().existsAsFile()) return;
                safe->proc.loadSoundFontAsync (fc.getResult());
                safe->proc.setSoundMode (MidiArchivProcessor::sndSoundFont);
                safe->setStatus (T ("SoundFont wird geladen: %1", fc.getResult().getFileName()));
            });
        }
        else if (r == 20) a.startScan (true);
        else if (r == 21) safe->setStatus (T ("%1 fehlende Einträge entfernt", a.removeMissing()));
        else if (r == 22)
        {
            auto& h = safe->table.list().getHeader();
            int pos = 0;
            for (auto* d = columnDefs(); d->id != 0; ++d) { h.setColumnVisible (d->id, true); h.moveColumn (d->id, pos++); h.setColumnWidth (d->id, d->width); }
        }
        else if (r == 30) a.databaseFile().getParentDirectory().startAsProcess();
        else if (r == 31) { backupFolder().createDirectory(); backupFolder().startAsProcess(); }
        a.saveSoon();
    });
}

// ============================================================================================ cell menu
void MidiArchivEditor::showCellMenu (Entry& e, int col, juce::Point<int>)
{
    const int id = e.id;
    const auto sel = table.selectedEntries();
    const auto query = cleanTitleForSearch (e.title + " " + e.artist);
    const bool editable = colDef (col).field >= 0;
    juce::PopupMenu m, web, deep;
    m.addSectionHeader (e.name);
    m.addItem (1, T ("Abspielen"));
    m.addItem (2, T ("Im Explorer zeigen"));
    {
        const int n = (int) targetsFor (e).size();
        juce::PopupMenu files;
        files.addItem (41, n > 1 ? T ("%1 markierte Dateien kopieren nach …", n) : T ("Datei kopieren nach …"), n > 0);
        files.addItem (42, n > 1 ? T ("%1 markierte Dateien verschieben nach …", n) : T ("Datei verschieben nach …"), n > 0);
        juce::PopupMenu convert;
        convert.addSectionHeader (n > 1 ? T ("%1 markierte Dateien konvertieren", n) : T ("Datei konvertieren"));
        convert.addItem (60, T ("MIDI-Datei (.mid) – für jede DAW: Ableton Live, Studio One / Fender Studio Pro, Reason, FL Studio, Cubase …"), n > 0);
        convert.addItem (61, T ("MusicXML (.musicxml) – für Notenprogramme: MuseScore 3 und neuer, Finale, Sibelius, Dorico …"), n > 0);
        convert.addItem (62, T ("ABC-Notation (.abc) – Melodie je Spur, für Folk-Programme und abcjs"), n > 0);
        convert.addSeparator();
        convert.addItem (63, T ("DAWproject (.dawproject) – Projekt mit Spuren, Tempo und Markern: Bitwig 5+, Studio One 6.5+ / Fender Studio Pro, Cubase 14+"), n > 0);
        convert.addItem (64, T ("REAPER-Projekt (.rpp) – eine MIDI-Spur je Instrument"), n > 0);
        convert.addItem (65, ma::daw::abletonInstalled() ? T ("Ableton Live Set (.als) – Songordner mit MIDI-Spuren, Tempo, Taktart, Locators und den GM-Klängen als Sampler")
                                                         : T ("Ableton Live Set (.als) – braucht ein installiertes Ableton Live"), n > 0 && ma::daw::abletonInstalled());
        files.addSubMenu (T ("Konvertieren nach"), convert, n > 0);
        files.addSeparator();
        files.addItem (40, n > 1 ? T ("%1 markierte Dateien löschen (Papierkorb) …", n) : T ("Datei löschen (Papierkorb) …"), n > 0);
        m.addSubMenu (n > 1 ? T ("Dateien (%1)", n) : T ("Datei"), files);
    }
    m.addSeparator();
    for (int i = 0; i < kNumEngines; ++i) web.addItem (100 + i, T (kEngineNames[i]) + U (":  „") + query + U ("“"));
    web.addSeparator();
    if (e.composer.isNotEmpty()) web.addItem (120, T ("Google: Komponist „%1“", e.composer));
    web.addItem (121, T ("Google: Titel + „Erscheinungsjahr“"));
    web.addItem (122, T ("Google: Titel + „Komponist“"));
    m.addSubMenu (T ("Internetrecherche"), web);
    m.addItem (10, T ("Fehlende Angaben online suchen (MusicBrainz + Wikidata) …"));
    deep.addItem (20, T ("Melodie im eigenen Archiv suchen …"));
    deep.addItem (21, T ("Melodie kopieren (Notennamen + Parsons-Code)"));
    deep.addItem (22, T ("Musipedia öffnen (Parsons-Code in der Zwischenablage)"));
    m.addSubMenu (T ("Deep-Modus (Melodie)"), deep);
    m.addSeparator();
    {
        juce::PopupMenu::Item u (arch->canUndo() ? T ("Rückgängig: %1", arch->undoLabel()) : T ("Rückgängig"));
        u.itemID = 50;
        u.isEnabled = arch->canUndo();
        u.shortcutKeyDescription = T ("Strg+Z");
        m.addItem (u);
    }
    if (editable)
    {
        m.addItem (30, T ("%1 bearbeiten", colName (col)));
        if (sel.size() > 1) m.addItem (31, T ("„%1“ auf alle %2 markierten übertragen", cellText (e, col), (int) sel.size()));
    }
    if (e.has (fTitleFromName) || e.has (fArtistFromName)) m.addItem (33, T ("Titel/Interpret aus dem Dateinamen in die Datei schreiben"));
    const auto text = cellText (e, col);
    if (text.isNotEmpty() && col != cProfile) m.addItem (32, T ("Als Suchfilter für „%1“", colName (col)));
    if (e.group != 0) m.addItem (34, T ("Alle Dubletten dieser Gruppe zeigen"));
    m.addItem (35, T ("Datei neu einlesen"));
    {
        juce::PopupMenu lp;
        addLoupeMenu (lp, 900);
        m.addSeparator();
        m.addSubMenu (T ("Lupe"), lp);
    }
    if (e.lyrics.isNotEmpty()) m.addItem (36, T ("Liedtext kopieren"));
    {
        int n = 0;
        for (auto* x : sel) n += x->key.isNotEmpty() ? 1 : 0;
        if (sel.size() > 1 && n > 0) m.addItem (37, T ("Ermittelte Tonart in alle %1 markierten Dateien eintragen", n));
        else if (e.key.isNotEmpty()) m.addItem (37, T ("Ermittelte Tonart „%1“ in die Datei eintragen", localKey (e.key.trimCharactersAtEnd ("?"))));
    }

    m.showMenuAsync (juce::PopupMenu::Options(), [safe = juce::Component::SafePointer<MidiArchivEditor> (this), id, col, query] (int r) {
        if (safe == nullptr || r == 0) return;
        auto& a = *safe->arch;
        auto* en = a.byId (id);
        if (en == nullptr) return;
        if (r >= 900 && r < 920) { safe->handleLoupeMenu (r, 900); return; }
        if (r == 1) safe->play (en);
        else if (r == 50) { safe->undo(); return; }
        else if (r == 2) juce::File (en->path).revealToUser();
        else if (r == 40) { safe->trashFiles (safe->targetsFor (*en)); return; }
        else if (r == 41 || r == 42) { safe->copyOrMoveFiles (safe->targetsFor (*en), r == 42); return; }
        else if (r >= 60 && r <= 65) { safe->convertFiles (safe->targetsFor (*en), r - 60); return; }
        else if (r >= 100 && r < 100 + kNumEngines) webSearchUrl (r - 100, query).launchInDefaultBrowser();
        else if (r == 120) webSearchUrl (0, en->composer + " " + T ("Komponist")).launchInDefaultBrowser();
        else if (r == 121) webSearchUrl (0, query + " " + T ("Erscheinungsjahr")).launchInDefaultBrowser();
        else if (r == 122) webSearchUrl (0, query + " " + T ("Komponist")).launchInDefaultBrowser();
        else if (r == 10) safe->startOnlineLookup (*en);
        else if (r == 20) safe->startMelodySearch (*en);
        else if (r == 21) safe->copyMelody (*en, false);
        else if (r == 22) safe->copyMelody (*en, true);
        else if (r == 30)
        {
            for (int i = 0; i < safe->table.numRows(); ++i)
                if (safe->table.rowEntry (i) == en) { safe->table.startEdit (i, col); break; }
        }
        else if (r == 31)
        {
            const auto value = col == cYear ? (en->year > 0 ? juce::String (en->year) : juce::String()) : cellText (*en, col);
            int ok = 0;
            Archive::UndoGroup g (a, T ("%1 in %2 Dateien", colName (col), (int) safe->table.selectedEntries().size() - 1));
            for (auto* other : safe->table.selectedEntries())
                if (other != en && safe->table.commitValue (*other, col, value)) ++ok;
            safe->setStatus (T ("%1 Dateien geändert", ok));
        }
        else if (r == 32) safe->table.footer->setFilter (col, "=" + cellText (*en, col));
        else if (r == 33)
        {
            juce::String err;
            const auto t = en->title, ar = en->artist;
            const bool ft = en->has (fTitleFromName), fa = en->has (fArtistFromName);
            Archive::UndoGroup g (a, T ("Titel/Interpret: %1", en->name));
            if (ft) a.setField (*en, Field::title, t, err);
            if (fa && err.isEmpty()) a.setField (*en, Field::artist, ar, err);
            safe->setStatus (err.isEmpty() ? T ("In die Datei geschrieben") : err);
        }
        else if (r == 34) safe->table.footer->setFilter (cDup, {}), safe->onlyDup.setToggleState (true, juce::sendNotification), safe->table.selectEntry (id);
        else if (r == 35) { a.reread (*en); safe->setStatus (T ("Neu eingelesen: %1", en->name)); }
        else if (r == 36) juce::SystemClipboard::copyTextToClipboard (en->lyrics);
        else if (r == 37)
        {
            auto targets = safe->table.selectedEntries();
            if (std::find (targets.begin(), targets.end(), en) == targets.end()) targets = { en };
            int ok = 0, same = 0;
            juce::String errors;
            Archive::UndoGroup g (a, T ("Tonart (Datei)") + ": " + (targets.size() > 1 ? T ("%1 Dateien", (int) targets.size()) : en->name));
            for (auto* x : targets)
            {
                const auto k = x->key.trimCharactersAtEnd ("?");
                if (k.isEmpty() || x->has (fMissing)) continue;
                if (k == x->keyMeta) { ++same; continue; }
                juce::String err;
                if (a.setField (*x, Field::keySignature, k, err)) ++ok; else errors << x->name << ": " << err << juce::newLine;
            }
            safe->setStatus (T ("Tonart in %1 Dateien eingetragen", ok) + (same > 0 ? T (", %1 stimmten schon", same) : juce::String()));
            if (errors.isNotEmpty()) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Midi-Fundus", errors.substring (0, 2000));
        }
        safe->refreshNow();
    });
}

void MidiArchivEditor::undo()
{
    if (! arch->canUndo()) { setStatus (T ("Nichts rückgängig zu machen")); return; }
    const auto label = arch->undoLabel();
    juce::String failed;
    arch->undo (failed);
    if (failed.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Midi-Fundus", T ("Konnte nicht zurückschreiben:") + juce::newLine + failed);
    setStatus (T ("Rückgängig gemacht: %1", label));
    refreshNow();
}

void MidiArchivEditor::copyMelody (Entry& e, bool openMusipedia)
{
    auto info = analyseMidiFile (juce::File (e.path), true);
    auto mel = extractMelody (info.notes, 24);
    if (mel.empty()) { setStatus (T ("Keine Melodiestimme gefunden")); return; }
    juce::StringArray names;
    for (auto& n : mel) names.add (noteName (n.pitch));
    const auto parsons = parsonsCode (mel);
    juce::SystemClipboard::copyTextToClipboard (openMusipedia ? parsons : names.joinIntoString (" ") + "\n" + parsons);
    setStatus (T ("In der Zwischenablage: %1", names.joinIntoString (" ") + "   " + parsons));
    if (openMusipedia) juce::URL ("https://www.musipedia.org/").launchInDefaultBrowser();
}

void MidiArchivEditor::startOnlineLookup (Entry& e)
{
    suggest = std::make_unique<ma::ui::SuggestPanel> (*this, e.id, T ("Online-Recherche: %1", e.title + (e.artist.isNotEmpty() ? U (" – ") + e.artist : juce::String())));
    addAndMakeVisible (*suggest);
    suggest->setBounds (getLocalBounds());
    suggest->setBusy (T ("Frage MusicBrainz und Wikidata …"));
    LookupQuery q { e.title, e.artist, e.composer };
    const int id = e.id;
    pool.addJob ([q, id, safe = juce::Component::SafePointer<MidiArchivEditor> (this)] {
        juce::String log;
        auto found = lookupOnline (q, log);
        juce::MessageManager::callAsync ([safe, id, found, log] {
            if (safe == nullptr || safe->suggest == nullptr || safe->suggest->entryId() != id) return;
            std::vector<ma::ui::SuggestPanel::Row> rows;
            for (auto& s : found) rows.push_back ({ s.field, s.value, s.source });
            safe->suggest->setResults (rows, log);
        });
    });
}

void MidiArchivEditor::startMelodySearch (Entry& e)
{
    suggest = std::make_unique<ma::ui::SuggestPanel> (*this, e.id, T ("Deep-Modus: Melodie von „%1“ im Archiv", e.name));
    addAndMakeVisible (*suggest);
    suggest->setBounds (getLocalBounds());
    suggest->setBusy (T ("Ziehe die Melodie heraus und vergleiche …"));
    // candidates: entries sharing sketch values (computed here, files are read in the background)
    struct Cand { int id; juce::String path; float score; };
    std::vector<Cand> cands;
    for (auto& up : arch->entries())
    {
        if (up->id == e.id || up->has (fMissing) || up->sketch.empty()) continue;
        size_t i = 0, j = 0; int shared = 0;
        while (i < e.sketch.size() && j < up->sketch.size())
        {
            if (e.sketch[i] < up->sketch[j]) ++i;
            else if (up->sketch[j] < e.sketch[i]) ++j;
            else { ++shared; ++i; ++j; }
        }
        if (shared > 0) cands.push_back ({ up->id, up->path, (float) shared });
    }
    std::sort (cands.begin(), cands.end(), [] (auto& x, auto& y) { return x.score > y.score; });
    if (cands.size() > 150) cands.resize (150);
    const auto path = e.path;
    const int id = e.id;
    pool.addJob ([cands, path, id, safe = juce::Component::SafePointer<MidiArchivEditor> (this)] {
        auto info = analyseMidiFile (juce::File (path), true);
        auto mel = extractMelody (info.notes, 400);
        auto melGrams = noteGrams (mel);
        std::vector<std::pair<int, float>> hits;
        juce::String log;
        log << T ("Melodie: %1 Noten, %2 Motive; %3 Kandidaten geprüft", (int) mel.size(), (int) melGrams.size(), (int) cands.size()) << "\n";
        if (! melGrams.empty())
            for (auto& c : cands)
            {
                auto other = analyseMidiFile (juce::File (c.path), true);
                if (! other.ok) continue;
                auto g = noteGrams (other.notes);
                int in = 0;
                for (auto x : melGrams) if (std::binary_search (g.begin(), g.end(), x)) ++in;
                const float share = (float) in / (float) melGrams.size();
                if (share >= 0.2f) hits.push_back ({ c.id, share });
            }
        std::sort (hits.begin(), hits.end(), [] (auto& x, auto& y) { return x.second > y.second; });
        juce::MessageManager::callAsync ([safe, id, hits, log] {
            if (safe == nullptr || safe->suggest == nullptr || safe->suggest->entryId() != id) return;
            std::vector<ma::ui::SuggestPanel::Row> rows;
            juce::String text (log);
            int shown = 0;
            for (auto& [oid, share] : hits)
            {
                auto* o = safe->arch->byId (oid);
                if (o == nullptr) continue;
                const auto src = T ("%1 % der Melodie in „%2“", juce::roundToInt (share * 100), o->name);
                text << src << "\n";
                if (++shown > 12) continue;
                if (! o->has (fTitleFromName) && o->title.isNotEmpty()) rows.push_back ({ Field::title, o->title, src });
                else if (o->title.isNotEmpty()) rows.push_back ({ Field::title, o->title, src + T (" (Dateiname)") });
                if (o->artist.isNotEmpty()) rows.push_back ({ Field::artist, o->artist, src });
                if (o->composer.isNotEmpty()) rows.push_back ({ Field::composer, o->composer, src });
                if (o->year > 0) rows.push_back ({ Field::year, juce::String (o->year), src });
                if (o->genre.isNotEmpty()) rows.push_back ({ Field::genre, o->genre, src });
            }
            if (hits.empty()) text << T ("Keine andere Datei enthält diese Melodie.");
            safe->suggest->setResults (rows, text);
        });
    });
}

// ============================================================================================ file operations
std::vector<int> MidiArchivEditor::targetsFor (Entry& clicked)
{
    auto sel = table.selectedEntries();
    if (std::find (sel.begin(), sel.end(), &clicked) == sel.end()) sel = { &clicked };
    std::vector<int> ids;
    for (auto* e : sel) if (! e->has (fMissing)) ids.push_back (e->id);
    return ids;
}

void MidiArchivEditor::trashFiles (const std::vector<int>& allIds, std::function<void()> after)
{
    std::vector<int> ids;
    int locked = 0;
    for (int id : allIds) if (auto* e = arch->byId (id); e != nullptr) { if (arch->isProtected (e->path)) ++locked; else ids.push_back (id); }
    if (locked > 0) setStatus (T ("%1 Datei(en) in schreibgeschützten Ordnern bleiben unangetastet", locked));
    if (ids.empty()) { if (locked > 0) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Midi-Fundus", arch->protectedMessage()); return; }
    juce::String list;
    int shown = 0;
    for (int id : ids)
        if (auto* e = arch->byId (id); e != nullptr && shown++ < 8) list << U ("• ") << e->name << "\n";
    if ((int) ids.size() > 8) list << T ("… und %1 weitere", (int) ids.size() - 8) << "\n";
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, T ("In den Papierkorb legen?"),
        (ids.size() == 1 ? T ("1 Datei in den Windows-Papierkorb legen (wiederherstellbar):")
                         : T ("%1 Dateien in den Windows-Papierkorb legen (wiederherstellbar):", (int) ids.size())) + "\n\n" + list,
        T ("Löschen"), T ("Abbrechen"), this,
        juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<MidiArchivEditor> (this), ids, after] (int r) {
            if (r == 0 || safe == nullptr) return;
            std::vector<int> gone;
            juce::String failed;
            for (int id : ids)
            {
                auto* e = safe->arch->byId (id);
                if (e == nullptr) continue;
                if (safe->proc.playingPath().equalsIgnoreCase (e->path)) safe->proc.stop();
                if (juce::File (e->path).moveToTrash()) gone.push_back (id);
                else failed << e->name << "\n";
            }
            safe->arch->removeEntries (gone);
            safe->setStatus (T ("%1 Datei(en) in den Papierkorb gelegt", (int) gone.size()));
            if (failed.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Midi-Fundus", T ("Nicht gelöscht:") + "\n" + failed.substring (0, 2000));
            safe->refreshNow();                                   // table drops the removed rows first
            safe->dups.setEntry (safe->table.selectedEntry());
            if (after) after();
        }));
}

// "Convert to …": every marked file as MIDI, MusicXML or ABC into a chosen folder (sources stay untouched)
void MidiArchivEditor::convertFiles (const std::vector<int>& ids, int format)
{
    std::vector<juce::String> paths;
    for (int id : ids) if (auto* e = arch->byId (id); e != nullptr && ! e->has (fMissing)) paths.push_back (e->path);
    if (paths.empty()) return;
    const auto fmt = static_cast<ExportFormat> (juce::jlimit (0, 5, format));   // order of ExportFormat
    const juce::File last (arch->settings.getProperty ("lastTarget").toString());

    // what happens after writing: status line, new files of archive folders into the archive, errors
    auto report = [safe = juce::Component::SafePointer<MidiArchivEditor> (this), fmt] (int ok, int added, const juce::String& where, const juce::String& failed)
    {
        if (safe == nullptr) return;
        juce::String msg = T ("%1 Datei(en) als %2 gespeichert in %3", ok, exportFormatName (fmt), where);
        if (added > 0) msg << T ("  (%1 neu im Archiv)", added);
        safe->setStatus (msg);
        if (failed.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Midi-Fundus", T ("Nicht konvertiert:") + "\n" + failed.substring (0, 2000));
        safe->refreshNow();
    };

    if (paths.size() == 1)
    {
        // one file: a real save dialog – the name is suggested and can be changed
        const juce::File src (paths.front());
        const auto suggested = (last.isDirectory() ? last : src.getParentDirectory()).getChildFile (suggestedExportName (src, fmt));
        const auto ext = exportExtension (fmt);
        chooser = std::make_unique<juce::FileChooser> (fmt == ExportFormat::ableton ? T ("Live Set speichern unter … (der Name wird zum Songordner)")
                                                                                    : T ("Konvertierte Datei speichern unter …"),
                                                       suggested, "*" + ext);
        const int flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                        | (fmt == ExportFormat::ableton ? 0 : juce::FileBrowserComponent::warnAboutOverwriting);
        chooser->launchAsync (flags, [safe = juce::Component::SafePointer<MidiArchivEditor> (this), src, fmt, ext, report] (const juce::FileChooser& fc) {
            auto target = fc.getResult();
            if (safe == nullptr || target == juce::File()) return;
            target = target.withFileExtension (ext);
            if (safe->arch->isProtected (target.getParentDirectory().getFullPathName()))
            { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Midi-Fundus", safe->arch->protectedMessage()); return; }
            safe->arch->settings.setProperty ("lastTarget", target.getParentDirectory().getFullPathName(), nullptr);
            auto run = [safe, src, fmt, target, report]
            {
                if (safe == nullptr) return;
                juce::File written;
                juce::String err;
                const bool ok = exportFileAs (src, fmt, target, written, err);
                const int added = ok && safe->arch->isInsideFolders (written) && safe->arch->addFile (written) != nullptr ? 1 : 0;
                report (ok ? 1 : 0, added, ok ? written.getFullPathName() : target.getFullPathName(), ok ? juce::String() : src.getFileName() + ": " + err + "\n");
            };
            // Ableton writes <name>/<name>.als: the dialog cannot see that file, so ask here
            const auto stem = juce::File::createLegalFileName (target.getFileNameWithoutExtension());
            const auto dir = target.getParentDirectory().getFileName() == stem ? target.getParentDirectory() : target.getParentDirectory().getChildFile (stem);
            if (fmt == ExportFormat::ableton && dir.getChildFile (stem + ".als").existsAsFile())
                juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, T ("Live Set ersetzen?"),
                    T ("„%1“ gibt es schon. Ersetzen? (Vorhandene Samples im Ordner bleiben und werden mitbenutzt.)", dir.getChildFile (stem + ".als").getFullPathName()),
                    T ("Ersetzen"), T ("Abbrechen"), safe.getComponent(),
                    juce::ModalCallbackFunction::create ([run] (int r) { if (r != 0) run(); }));
            else run();
        });
        return;
    }

    // several files: a folder, each file gets its own name ("Artist - Title")
    chooser = std::make_unique<juce::FileChooser> (T ("Konvertierte Dateien speichern nach …"), last);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safe = juce::Component::SafePointer<MidiArchivEditor> (this), paths, fmt, report] (const juce::FileChooser& fc) {
            const auto dir = fc.getResult();
            if (safe == nullptr || ! dir.isDirectory()) return;
            if (safe->arch->isProtected (dir.getFullPathName()))
            { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Midi-Fundus", safe->arch->protectedMessage()); return; }
            safe->arch->settings.setProperty ("lastTarget", dir.getFullPathName(), nullptr);
            int ok = 0, added = 0;
            juce::String failed;
            for (auto& p : paths)
            {
                juce::File written;
                juce::String err;
                if (exportFile (juce::File (p), fmt, dir, written, err))
                {
                    ++ok;
                    if (safe->arch->isInsideFolders (written) && safe->arch->addFile (written) != nullptr) ++added;
                }
                else failed << juce::File (p).getFileName() << ": " << err << "\n";
            }
            report (ok, added, dir.getFullPathName(), failed);
        });
}

void MidiArchivEditor::copyOrMoveFiles (const std::vector<int>& ids, bool move)
{
    if (ids.empty()) return;
    chooser = std::make_unique<juce::FileChooser> (move ? T ("Verschieben nach …") : T ("Kopieren nach …"),
                                                   juce::File (arch->settings.getProperty ("lastTarget").toString()));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safe = juce::Component::SafePointer<MidiArchivEditor> (this), ids, move] (const juce::FileChooser& fc) {
            const auto dir = fc.getResult();
            if (safe == nullptr || ! dir.isDirectory()) return;
            auto& a = *safe->arch;
            if (a.isProtected (dir.getFullPathName()))
            { juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Midi-Fundus", a.protectedMessage()); return; }
            a.settings.setProperty ("lastTarget", dir.getFullPathName(), nullptr);
            int ok = 0, renamed = 0, added = 0;
            juce::String failed;
            for (int id : ids)
            {
                auto* e = a.byId (id);
                if (e == nullptr) continue;
                const juce::File src (e->path);
                if (src.getParentDirectory() == dir) { failed << T ("%1 (liegt schon dort)", e->name) << "\n"; continue; }
                auto dst = dir.getChildFile (src.getFileName());
                if (dst.exists()) { dst = dst.getNonexistentSibling (true); ++renamed; }
                if (move && a.isProtected (e->path)) { failed << T ("%1 (schreibgeschützter Ordner)", e->name) << "\n"; continue; }
                if (move)
                {
                    if (safe->proc.playingPath().equalsIgnoreCase (e->path)) safe->proc.stop();
                    bool done = src.moveFileTo (dst);
                    if (! done && src.copyFileTo (dst)) done = src.deleteFile();
                    if (done) { a.relocate (*e, dst); ++ok; }
                    else failed << e->name << "\n";
                }
                else
                {
                    if (src.copyFileTo (dst))
                    {
                        ++ok;
                        if (a.isInsideFolders (dst) && a.addFile (dst) != nullptr) ++added;
                    }
                    else failed << e->name << "\n";
                }
            }
            juce::String msg;
            msg << (move ? T ("%1 Datei(en) verschoben nach %2", ok, dir.getFullPathName()) : T ("%1 Datei(en) kopiert nach %2", ok, dir.getFullPathName()));
            if (renamed > 0) msg << T ("  (%1 umbenannt, Name war schon vergeben)", renamed);
            if (added > 0) msg << T ("  (%1 neu im Archiv)", added);
            safe->setStatus (msg);
            if (failed.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Midi-Fundus",
                                                        (move ? T ("Nicht verschoben:") : T ("Nicht kopiert:")) + "\n" + failed.substring (0, 2000));
            safe->refreshNow();
        });
}
