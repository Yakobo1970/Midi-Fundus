#include "DupView.h"
#include "Look.h"
#include <map>

namespace ma
{
using ui::font;

// ============================================================================================ alignment
Comparison compareFiles (const juce::File& fa, const juce::File& fb)
{
    Comparison c;
    c.a = analyseMidiFile (fa, true);
    c.b = analyseMidiFile (fb, true);
    if (! c.a.ok || ! c.b.ok) return c;
    auto& A = c.a.notes;
    auto& B = c.b.notes;

    // transposition: best rotation of the 12-tone profiles
    double best = -1e9;
    for (int s = -6; s <= 5; ++s)
    {
        double sum = 0;
        for (int i = 0; i < 12; ++i) sum += (double) c.a.pitchClasses[(size_t) i] * c.b.pitchClasses[(size_t) ((i - s + 24) % 12)];
        if (sum > best) { best = sum; c.transpose = s; }
    }
    // octave: compare mean pitch
    {
        double ma = 0, mb = 0; int na = 0, nb = 0;
        for (auto& n : A) if (n.ch != 9) { ma += n.pitch; ++na; }
        for (auto& n : B) if (n.ch != 9) { mb += n.pitch; ++nb; }
        if (na > 0 && nb > 0)
        {
            const double diff = ma / na - (mb / nb + c.transpose);
            c.transpose += 12 * juce::roundToInt (diff / 12.0);
        }
    }
    // time offset: cross-correlate onset histograms (1/4 beat bins), pitch-aware
    {
        constexpr int bins = 2048, maxShift = 512;
        std::vector<std::array<juce::uint16, 12>> ha (bins), hb (bins);
        for (auto& n : A) if (n.ch != 9) { const int b = (int) (n.start * 4); if (b >= 0 && b < bins) ++ha[(size_t) b][(size_t) (n.pitch % 12)]; }
        for (auto& n : B) if (n.ch != 9) { const int b = (int) (n.start * 4); if (b >= 0 && b < bins) ++hb[(size_t) b][(size_t) ((n.pitch + c.transpose + 120) % 12)]; }
        long bestScore = -1; int bestShift = 0;
        for (int s = -maxShift; s <= maxShift; ++s)
        {
            long score = 0;
            for (int i = juce::jmax (0, s); i < juce::jmin (bins, bins + s); ++i)
            {
                const auto& x = ha[(size_t) i];
                const auto& y = hb[(size_t) (i - s)];
                for (int k = 0; k < 12; ++k) score += juce::jmin (x[(size_t) k], y[(size_t) k]);
            }
            if (score > bestScore || (score == bestScore && std::abs (s) < std::abs (bestShift))) { bestScore = score; bestShift = s; }
        }
        c.offset = bestShift / 4.0f;
    }
    // match notes
    std::map<int, std::vector<size_t>> bByPitch;
    for (size_t i = 0; i < B.size(); ++i) bByPitch[B[i].pitch + c.transpose + (B[i].ch == 9 ? 1000 : 0) - (B[i].ch == 9 ? c.transpose : 0)].push_back (i);
    c.aMatched.assign (A.size(), false);
    c.bMatched.assign (B.size(), false);
    int ma = 0, mb = 0, na = 0, nb = 0;
    for (size_t i = 0; i < A.size(); ++i)
    {
        const int key = A[i].pitch + (A[i].ch == 9 ? 1000 : 0);
        auto it = bByPitch.find (key);
        if (it == bByPitch.end()) continue;
        const float t = A[i].start;
        auto& v = it->second;
        auto lo = std::lower_bound (v.begin(), v.end(), t - 0.13f, [&] (size_t idx, float x) { return B[idx].start + c.offset < x; });
        for (auto p = lo; p != v.end() && B[*p].start + c.offset <= t + 0.13f; ++p)
        {
            c.aMatched[i] = true;
            c.bMatched[*p] = true;
        }
    }
    for (size_t i = 0; i < A.size(); ++i) if (A[i].ch != 9) { ++na; ma += c.aMatched[i] ? 1 : 0; }
    for (size_t i = 0; i < B.size(); ++i) if (B[i].ch != 9) { ++nb; mb += c.bMatched[i] ? 1 : 0; }
    c.aShare = na > 0 ? (float) ma / na : 0;
    c.bShare = nb > 0 ? (float) mb / nb : 0;
    c.valid = true;
    return c;
}

void drawMountain (juce::Graphics& g, const Entry& e, juce::Rectangle<float> r, const Theme& th, bool outline)
{
    const float bw = r.getWidth() / (float) (kMountBins - 1);
    for (int t = kMountTracks - 1; t >= 0; --t)
    {
        juce::Path p;
        p.startNewSubPath (r.getX(), r.getBottom());
        for (int b = 0; b < kMountBins; ++b)
        {
            float s = 0;
            for (int k = 0; k <= t; ++k) s += e.mountain[(size_t) (b * kMountTracks + k)];
            p.lineTo (r.getX() + b * bw, r.getBottom() - juce::jmin (1.0f, s / 255.0f) * r.getHeight());
        }
        p.lineTo (r.getRight(), r.getBottom());
        p.closeSubPath();
        g.setColour (th.track[(size_t) t].withAlpha (0.85f));
        g.fillPath (p);
        if (outline) { g.setColour (th.bg.withAlpha (0.5f)); g.strokePath (p, juce::PathStrokeType (1.0f)); }
    }
}

// ============================================================================================ view
void DupView::applyLanguage()
{
    playA.setButtonText (juce::String::fromUTF8 ("▶ A"));
    playB.setButtonText (juce::String::fromUTF8 ("▶ B"));
    showB.setButtonText (T ("B in Tabelle"));
    notDup.setButtonText (T ("Kein Duplikat"));
    trashA.setButtonText (T ("A in Papierkorb"));
    trashB.setButtonText (T ("B in Papierkorb"));
    playA.setTooltip (T ("Datei A (in der Tabelle gewählt) anhören"));
    playB.setTooltip (T ("Vergleichsdatei B anhören"));
    showB.setTooltip (T ("Datei B in der Tabelle auswählen"));
    notDup.setTooltip (T ("A und B sind keine Dubletten – Verbindung dauerhaft lösen"));
    trashA.setTooltip (T ("Datei A (in der Tabelle gewählt) in den Windows-Papierkorb legen (wiederherstellbar) – danach geht es mit B weiter"));
    trashB.setTooltip (T ("Datei B in den Windows-Papierkorb legen (wiederherstellbar)"));
    members.repaint();
    repaint();
}

DupView::DupView (Archive& a, std::function<const Theme&()> t) : arch (a), theme (std::move (t))
{
    members.setRowHeight (34);
    members.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (members);
    for (auto* b : { &playA, &playB, &showB, &notDup, &trashA, &trashB }) addAndMakeVisible (b);
    applyLanguage();
    playA.onClick = [this] { if (onPlay) onPlay (arch.byId (idA)); };
    playB.onClick = [this] { if (onPlay) onPlay (arch.byId (idB)); };
    showB.onClick = [this] { if (onSelectInTable && idB != 0) onSelectInTable (idB); };
    notDup.onClick = [this] {
        if (idA == 0 || idB == 0) return;
        const int row = juce::jmax (0, members.getSelectedRow());
        const auto* b = arch.byId (idB);
        const juce::String nameB = b != nullptr ? b->name : juce::String();
        arch.markNotDuplicate (idA, idB);
        setEntry (arch.byId (idA));
        if (! ids.empty())
        {
            const int r = juce::jmin (row, (int) ids.size() - 1);   // continue with the file that followed B
            members.selectRow (r);
            choose (ids[(size_t) r]);
        }
        if (onStatus) onStatus (T ("Getrennt: %1", nameB));
    };
    trashB.onClick = [this] {
        if (idB == 0 || ! onTrash) return;
        const int row = juce::jmax (0, members.getSelectedRow());
        onTrash (idB, [safe = juce::Component::SafePointer<DupView> (this), row, a = idA] {
            if (safe == nullptr) return;
            safe->setEntry (safe->arch.byId (a));
            if (! safe->ids.empty())
            {
                const int r = juce::jmin (row, (int) safe->ids.size() - 1);   // continue with the next member
                safe->members.selectRow (r);
                safe->choose (safe->ids[(size_t) r]);
            }
        });
    };
    trashA.onClick = [this] {
        if (idA == 0 || ! onTrash) return;
        const int next = idB;     // carry on with B as the new A
        onTrash (idA, [safe = juce::Component::SafePointer<DupView> (this), next] {
            if (safe == nullptr || next == 0 || safe->arch.byId (next) == nullptr) return;
            if (safe->onSelectInTable) safe->onSelectInTable (next);
            safe->setEntry (safe->arch.byId (next));
        });
    };
    setEntry (nullptr);
}

void DupView::setEntry (Entry* e)
{
    ids.clear();
    idA = e != nullptr ? e->id : 0;
    idB = 0;
    cmp = {};
    zoom = 1.0f; scroll = 0.0f;
    if (e != nullptr && e->group != 0)
    {
        auto links = arch.linksOf (e->id);
        std::sort (links.begin(), links.end(), [] (auto& x, auto& y) { return x.kind != y.kind ? x.kind > y.kind : x.score > y.score; });
        for (auto& l : links) ids.push_back (l.other);
        for (int id : arch.groupMembers (e->group))
            if (id != e->id && std::find (ids.begin(), ids.end(), id) == ids.end()) ids.push_back (id);
    }
    members.updateContent();
    members.repaint();
    if (! ids.empty()) { members.selectRow (0); choose (ids[0]); }
    for (auto* b : { &playB, &showB, &notDup, &trashB }) b->setEnabled (idB != 0);
    playA.setEnabled (idA != 0);
    trashA.setEnabled (idA != 0);
    repaint();
}

void DupView::refreshList() { members.repaint(); }

void DupView::choose (int b)
{
    idB = b;
    auto* ea = arch.byId (idA);
    auto* eb = arch.byId (idB);
    if (ea != nullptr && eb != nullptr) cmp = compareFiles (juce::File (ea->path), juce::File (eb->path));
    for (auto* bt : { &playB, &showB, &notDup, &trashB }) bt->setEnabled (idB != 0);
    repaint();
}

void DupView::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    const auto& th = theme();
    if (selected) { g.setColour (th.accent.withAlpha (0.3f)); g.fillRect (0, 0, w, h); }
    if (row < 0 || row >= (int) ids.size()) return;
    auto* e = arch.byId (ids[(size_t) row]);
    if (e == nullptr) return;
    juce::String kind;
    for (auto& l : arch.linksOf (idA))
        if (l.other == e->id)
            kind = l.kind == dupIdentical ? T ("identisch") : l.kind == dupSameNotes ? T ("gleiche Noten") : T ("ähnlich %1 %", juce::roundToInt (l.score * 100));
    if (kind.isEmpty()) kind = T ("über die Gruppe verbunden");
    drawMountain (g, *e, { 4.0f, 4.0f, 60.0f, (float) h - 8 }, th, false);
    g.setColour (th.text);
    g.setFont (font (12.5f, true));
    g.drawText (e->name, 70, 2, w - 74, h / 2, juce::Justification::centredLeft, true);
    g.setColour (e->dupKind == dupSimilar ? th.track[1] : th.track[0]);
    g.setFont (font (11.5f));
    g.drawText (kind + juce::String::fromUTF8 ("  ·  ") + e->folder, 70, h / 2, w - 74, h / 2 - 2, juce::Justification::centredLeft, true);
}

void DupView::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < (int) ids.size()) choose (ids[(size_t) row]);
}

void DupView::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < (int) ids.size() && onPlay) onPlay (arch.byId (ids[(size_t) row]));
}

void DupView::resized()
{
    auto r = getLocalBounds().reduced (6);
    auto left = r.removeFromLeft (juce::jmin (330, r.getWidth() / 3));
    auto btns = left.removeFromBottom (28);
    const int bw = (btns.getWidth() - 8) / 3;
    auto row2 = left.removeFromBottom (32);
    row2.removeFromBottom (4);
    playA.setBounds (btns.removeFromLeft (bw)); btns.removeFromLeft (4);
    playB.setBounds (btns.removeFromLeft (bw)); btns.removeFromLeft (4);
    showB.setBounds (btns);
    notDup.setBounds (row2.removeFromLeft (bw)); row2.removeFromLeft (4);
    trashA.setBounds (row2.removeFromLeft (bw)); row2.removeFromLeft (4);
    trashB.setBounds (row2);
    members.setBounds (left.reduced (0, 2));
    r.removeFromLeft (8);
    infoArea = r.removeFromTop (22);
    auto top = r.removeFromTop (juce::jmin (110, r.getHeight() / 3));
    const int mw = (top.getWidth() - 200) / 2;
    mountA = top.removeFromLeft (mw);
    mountB = top.removeFromRight (mw);
    pcArea = top.reduced (8, 0);
    r.removeFromTop (6);
    rollArea = r;
}

void DupView::paint (juce::Graphics& g)
{
    const auto& th = theme();
    g.fillAll (th.panel);
    auto* ea = arch.byId (idA);
    auto* eb = arch.byId (idB);
    g.setFont (font (13.0f));
    if (ea == nullptr || eb == nullptr)
    {
        g.setColour (th.dim);
        g.drawFittedText (ea == nullptr ? T ("Datei in der Tabelle wählen – hier erscheinen ihre Dubletten.")
                                        : T ("Zu „%1“ wurden keine Dubletten gefunden.", ea->name),
                          getLocalBounds().withTrimmedLeft (juce::jmin (330, getWidth() / 3) + 14).reduced (10), juce::Justification::centred, 3);
        if (ea != nullptr && ! mountA.isEmpty())
            drawMountain (g, *ea, mountA.toFloat().reduced (2), th, true);
        return;
    }
    // info line
    juce::String info;
    if (cmp.valid)
    {
        info = T ("A: %1 % der Noten auch in B  ·  B: %2 % auch in A  ·  B transponiert %3 HT, versetzt %4 Viertel",
                  juce::roundToInt (cmp.aShare * 100), juce::roundToInt (cmp.bShare * 100),
                  (cmp.transpose > 0 ? "+" : "") + juce::String (cmp.transpose), juce::String (cmp.offset, 2));
    }
    else info = T ("Dateien konnten nicht verglichen werden");
    g.setColour (th.text);
    g.drawText (info, infoArea, juce::Justification::centredLeft, true);

    auto box = [&] (juce::Rectangle<int> r, const Entry& e, const juce::String& label)
    {
        g.setColour (th.panel2.withAlpha (0.6f));
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        drawMountain (g, e, r.toFloat().reduced (6, 18).withTrimmedBottom (-12), th, true);
        g.setColour (th.text);
        g.setFont (font (12.0f, true));
        g.drawText (label + "  " + e.name, r.reduced (8, 2).removeFromTop (16), juce::Justification::centredLeft, true);
    };
    box (mountA, *ea, "A");
    box (mountB, *eb, "B");
    drawPitchClasses (g, pcArea);
    drawRoll (g, rollArea);
}

void DupView::drawPitchClasses (juce::Graphics& g, juce::Rectangle<int> r)
{
    const auto& th = theme();
    auto* ea = arch.byId (idA);
    auto* eb = arch.byId (idB);
    if (ea == nullptr || eb == nullptr) return;
    g.setColour (th.panel2.withAlpha (0.6f));
    g.fillRoundedRectangle (r.toFloat(), 6.0f);
    auto area = r.reduced (6, 4);
    g.setColour (th.dim);
    g.setFont (font (11.0f));
    g.drawText (T ("12 Töne (B transponiert)"), area.removeFromTop (14), juce::Justification::centred);
    const float w = area.getWidth() / 12.0f;
    static const char* names[12] = { "C", "", "D", "", "E", "F", "", "G", "", "A", "", "H" };
    auto labels = area.removeFromBottom (12);
    for (int i = 0; i < 12; ++i)
    {
        const float x = area.getX() + i * w;
        const float ha = ea->pitchClasses[(size_t) i] / 255.0f * area.getHeight();
        const float hb = eb->pitchClasses[(size_t) ((i - cmp.transpose + 120) % 12)] / 255.0f * area.getHeight();
        g.setColour (th.track[3].withAlpha (0.9f));
        g.fillRect (x + 1, area.getBottom() - ha, w * 0.45f - 1, ha);
        g.setColour (th.track[5].withAlpha (0.9f));
        g.fillRect (x + w * 0.5f, area.getBottom() - hb, w * 0.45f - 1, hb);
        g.setColour (th.dim);
        g.drawText (names[i], juce::Rectangle<float> (x, (float) labels.getY(), w, 12.0f), juce::Justification::centred);
    }
}

void DupView::drawRoll (juce::Graphics& g, juce::Rectangle<int> r)
{
    const auto& th = theme();
    g.setColour (th.bg);
    g.fillRoundedRectangle (r.toFloat(), 6.0f);
    if (! cmp.valid) return;
    auto& A = cmp.a.notes;
    auto& B = cmp.b.notes;
    int lo = 127, hi = 0;
    float endBeat = 1;
    for (auto& n : A) if (n.ch != 9) { lo = juce::jmin (lo, (int) n.pitch); hi = juce::jmax (hi, (int) n.pitch); endBeat = juce::jmax (endBeat, n.start + n.len); }
    for (auto& n : B) if (n.ch != 9) { const int p = n.pitch + cmp.transpose; lo = juce::jmin (lo, p); hi = juce::jmax (hi, p); endBeat = juce::jmax (endBeat, n.start + cmp.offset + n.len); }
    if (hi < lo) { lo = 36; hi = 84; }
    lo -= 1; hi += 1;
    const auto inner = r.reduced (4).toFloat();
    const float drumH = 14.0f;
    const auto pitchArea = inner.withTrimmedBottom (drumH + 4);
    const float rowH = pitchArea.getHeight() / (float) (hi - lo + 1);
    const float visibleBeats = endBeat / zoom;
    scroll = juce::jlimit (0.0f, juce::jmax (0.0f, endBeat - visibleBeats), scroll);
    const float pxPerBeat = inner.getWidth() / visibleBeats;
    auto xOf = [&] (float beat) { return inner.getX() + (beat - scroll) * pxPerBeat; };
    auto yOf = [&] (int pitch) { return pitchArea.getBottom() - (pitch - lo + 1) * rowH; };

    g.saveState();
    g.reduceClipRegion (r.reduced (2));
    // octave lines and bars (4 beats)
    g.setColour (th.panel2.withAlpha (0.5f));
    for (int p = lo; p <= hi; ++p) if (p % 12 == 0) g.drawHorizontalLine ((int) yOf (p) + (int) rowH, inner.getX(), inner.getRight());
    const int barStep = pxPerBeat * 4 < 12 ? 16 : 4;
    for (int b = 0; b <= (int) endBeat; b += barStep)
    {
        const float x = xOf ((float) b);
        if (x >= inner.getX() && x <= inner.getRight()) g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom());
    }
    const auto green = juce::Colour (0xff48ba68), red = juce::Colour (0xffd6403a);
    // B: outlined (grey where unmatched)
    for (size_t i = 0; i < B.size(); ++i)
    {
        const auto& n = B[i];
        const float x = xOf (n.start + cmp.offset), w = juce::jmax (1.5f, n.len * pxPerBeat);
        if (x > inner.getRight() || x + w < inner.getX()) continue;
        const float y = n.ch == 9 ? inner.getBottom() - drumH * 0.5f : yOf (n.pitch + cmp.transpose);
        const float h = n.ch == 9 ? drumH * 0.5f : juce::jmax (1.5f, rowH);
        g.setColour (cmp.bMatched[i] ? green.withAlpha (0.35f) : th.dim.withAlpha (0.55f));
        g.drawRect (x, y, w, h, 1.0f);
    }
    // A: filled, green = also in B, red = only in A
    for (size_t i = 0; i < A.size(); ++i)
    {
        const auto& n = A[i];
        const float x = xOf (n.start), w = juce::jmax (1.5f, n.len * pxPerBeat);
        if (x > inner.getRight() || x + w < inner.getX()) continue;
        const float y = n.ch == 9 ? inner.getBottom() - drumH : yOf (n.pitch);
        const float h = n.ch == 9 ? drumH * 0.5f : juce::jmax (1.5f, rowH - 0.5f);
        g.setColour ((cmp.aMatched[i] ? green : red).withAlpha (0.85f));
        g.fillRect (x, y, w, h);
    }
    g.restoreState();
    g.setColour (th.dim);
    g.setFont (font (11.0f));
    g.drawText (T ("Gefüllt = A (grün: auch in B, rot: nur in A)  ·  Umriss = B  ·  Mausrad zoomt, Ziehen verschiebt"),
                r.reduced (8, 4).removeFromTop (14), juce::Justification::topRight);
}

void DupView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! rollArea.contains (e.getPosition()) || ! cmp.valid) return;
    float endBeat = 1;
    for (auto& n : cmp.a.notes) endBeat = juce::jmax (endBeat, n.start + n.len);
    const float frac = (e.position.x - rollArea.getX()) / (float) rollArea.getWidth();
    const float beatAtMouse = scroll + frac * endBeat / zoom;
    zoom = juce::jlimit (1.0f, 64.0f, zoom * (w.deltaY > 0 ? 1.25f : 0.8f));
    scroll = beatAtMouse - frac * endBeat / zoom;
    repaint (rollArea);
}

void DupView::mouseDown (const juce::MouseEvent&) { dragStartScroll = scroll; }

void DupView::mouseDrag (const juce::MouseEvent& e)
{
    if (! rollArea.contains (e.getMouseDownPosition()) || ! cmp.valid) return;
    float endBeat = 1;
    for (auto& n : cmp.a.notes) endBeat = juce::jmax (endBeat, n.start + n.len);
    scroll = dragStartScroll - (float) e.getDistanceFromDragStartX() / (float) rollArea.getWidth() * endBeat / zoom;
    repaint (rollArea);
}
} // namespace ma
