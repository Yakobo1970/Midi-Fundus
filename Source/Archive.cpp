#include "Archive.h"
#include "Lang.h"

namespace ma
{
struct DupIndex
{
    struct FP { juce::uint32 crc; juce::uint64 noteHash; int notes, gramCount; std::vector<juce::uint32> sketch; bool error; };
    std::unordered_map<juce::uint32, std::vector<int>> crcIdx, gramIdx;
    std::unordered_map<juce::uint64, std::vector<int>> noteIdx;
    std::unordered_map<int, std::vector<DupLink>> links;
    std::unordered_map<int, int> parent;
    std::unordered_map<int, FP> fps;
    std::unordered_map<int, int> shared;
    std::set<juce::uint64> notDup;
    float threshold = 0.45f;
    void add (int id, const FP& fp);
    void remove (int id);
    int find (int id);
    void unite (int a, int b);
    std::vector<juce::uint64> separate (int a, int b);   // links to cut so a and b end in different groups
    void cut (const std::vector<juce::uint64>& pairs);
    static juce::uint64 pairKey (int a, int b) { return a < b ? ((juce::uint64) (juce::uint32) a << 32) | (juce::uint32) b : ((juce::uint64) (juce::uint32) b << 32) | (juce::uint32) a; }
};

static DupIndex::FP fingerprintOf (const Entry& e)
{
    return { e.crc, e.noteHash, e.notes, e.gramCount, e.sketch, e.has (fError) };
}

bool isMidiFileName (const juce::String& name)
{
    const auto n = name.toLowerCase();
    return n.endsWith (".mid") || n.endsWith (".midi") || n.endsWith (".kar") || n.endsWith (".rmi") || n.endsWith (".smf");
}

juce::String formatDuration (double s)
{
    const int t = juce::roundToInt (s);
    return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2);
}

void Entry::fill (const MidiInfo& m)
{
    title = m.title; artist = m.artist; composer = m.composer; genre = m.genre; copyright = m.copyright;
    comment = m.comment; language = m.language; lyrics = m.lyrics;
    key = m.key; keyMeta = m.keyMeta; scale = m.scale; timeSig = m.timeSig; instruments = m.instruments; error = m.error;
    year = m.year; format = m.format; tracks = m.numTracks; channels = m.channelCount; notes = m.noteCount; drumNotes = m.drumNotes;
    bpm = (float) m.bpm; seconds = (float) m.seconds; keyConfidence = m.keyConfidence;
    noteHash = m.noteHash; sketch = m.sketch; gramCount = m.gramCount; mountain = m.mountain; pitchClasses = m.pitchClasses;
    flags &= ~(juce::uint32) (fTitleFromName | fArtistFromName | fError | fKaraoke | fRmi | fTempoChanges);
    if (! m.ok) flags |= fError;
    if (m.karaoke) flags |= fKaraoke;
    if (m.rmi) flags |= fRmi;
    if (m.tempoChanges) flags |= fTempoChanges;

    // fall back to the file name: "Artist - Title.mid"
    auto base = juce::File (path).getFileNameWithoutExtension().replaceCharacter ('_', ' ').trim();
    juce::String fa, ft = base;
    const int dash = base.indexOf (" - ");
    if (dash > 0) { fa = base.substring (0, dash).trim(); ft = base.substring (dash + 3).trim(); }
    if (title.isEmpty() && ft.isNotEmpty()) { title = ft; flags |= fTitleFromName; }
    if (artist.isEmpty() && fa.isNotEmpty()) { artist = fa; flags |= fArtistFromName; }
}

// ============================================================================================ scanner
struct Archive::Scanner : public juce::Thread
{
    Scanner (Archive& a, juce::StringArray roots, bool full) : juce::Thread ("MIDIARCHIV scan"), arch (a), folders (std::move (roots)), fullRescan (full)
    {
        for (auto& e : a.list) known[e->path.toLowerCase()] = { e->size, e->mtime };
    }

    void run() override
    {
        // 1) collect files
        juce::Array<juce::File> files;
        for (auto& f : folders)
        {
            for (const auto& it : juce::RangedDirectoryIterator (juce::File (f), true, "*", juce::File::findFiles))
            {
                if (threadShouldExit()) return;
                if (isMidiFileName (it.getFile().getFileName())) { files.add (it.getFile()); arch.total = files.size(); }
            }
        }
        {
            const juce::ScopedLock sl (arch.pendingLock);
            phase = 1;
        }
        // 2) analyse in parallel (unchanged files are only marked as seen)
        std::atomic<int> next { 0 };
        const int workers = juce::jlimit (2, 12, juce::SystemStats::getNumCpus());
        juce::ThreadPool pool (workers);
        for (int w = 0; w < workers; ++w)
            pool.addJob ([this, &files, &next] {
                for (;;)
                {
                    if (threadShouldExit()) return;
                    const int i = next++;
                    if (i >= files.size()) return;
                    const auto& f = files.getReference (i);
                    const auto path = f.getFullPathName();
                    const auto size = f.getSize();
                    const auto mtime = f.getLastModificationTime().toMilliseconds();
                    {
                        const juce::ScopedLock sl (arch.pendingLock);
                        arch.seenPaths.add (path.toLowerCase());
                    }
                    auto it = known.find (path.toLowerCase());
                    if (! fullRescan && it != known.end() && it->second.first == size && it->second.second == mtime) { ++arch.done; continue; }
                    auto e = std::make_unique<Entry>();
                    e->path = path; e->name = f.getFileName(); e->folder = f.getParentDirectory().getFullPathName();
                    e->size = size; e->mtime = mtime;
                    juce::MemoryBlock mb;
                    if (f.loadFileAsData (mb))
                    {
                        e->crc = crc32 (mb.getData(), mb.getSize());
                        e->fill (analyseMidi (mb));
                    }
                    else
                    {
                        MidiInfo bad;
                        bad.error = juce::String::fromUTF8 ("Datei nicht lesbar");
                        e->fill (bad);
                    }
                    {
                        const juce::ScopedLock sl (arch.pendingLock);
                        arch.pending.push_back (std::move (e));
                    }
                    ++arch.done;
                }
            });
        while (pool.getNumJobs() > 0 && ! threadShouldExit()) wait (50);
        if (threadShouldExit()) pool.removeAllJobs (true, 10000);
        finished = ! threadShouldExit();
    }

    Archive& arch;
    juce::StringArray folders;
    bool fullRescan;
    std::unordered_map<juce::String, std::pair<juce::int64, juce::int64>> known;
    std::atomic<int> phase { 0 };
    std::atomic<bool> finished { false };
};

// ============================================================================================ archive
Archive::Archive()
{
    load();
    initLanguage (settings);
    startIndexBuild();
    startTimer (250);
}

Archive::~Archive()
{
    stopTimer();
    if (scanner) { scanner->stopThread (15000); scanner.reset(); }
    if (indexThread.joinable()) indexThread.join();
    if (dirty) save();
}

Entry* Archive::byId (int id) const
{
    auto it = idIndex.find (id);
    return it != idIndex.end() ? it->second : nullptr;
}

Entry* Archive::byPath (const juce::String& path) const
{
    auto it = pathIndex.find (path.toLowerCase());
    return it != pathIndex.end() ? it->second : nullptr;
}

void Archive::addFolder (const juce::File& dir)
{
    const auto p = dir.getFullPathName();
    for (auto& f : folders) if (p.equalsIgnoreCase (f) || p.startsWithIgnoreCase (f + juce::File::getSeparatorString())) return;
    // a new parent folder replaces its sub folders
    for (int i = folders.size(); --i >= 0;)
        if (folders[i].startsWithIgnoreCase (p + juce::File::getSeparatorString())) folders.remove (i);
    folders.add (p);
    saveSoon();
    changed();
}

void Archive::removeFolder (const juce::String& path, bool removeEntries)
{
    folders.removeString (path, true);
    if (removeEntries)
    {
        const auto prefix = path + juce::File::getSeparatorString();
        list.erase (std::remove_if (list.begin(), list.end(), [&] (auto& e) { return e->path.startsWithIgnoreCase (prefix); }), list.end());
        pathIndex.clear(); idIndex.clear();
        for (auto& e : list) { pathIndex[e->path.toLowerCase()] = e.get(); idIndex[e->id] = e.get(); }
        startIndexBuild();
    }
    saveSoon();
    changed();
}

int Archive::removeMissing()
{
    const auto before = list.size();
    list.erase (std::remove_if (list.begin(), list.end(), [] (auto& e) { return e->has (fMissing); }), list.end());
    pathIndex.clear(); idIndex.clear();
    for (auto& e : list) { pathIndex[e->path.toLowerCase()] = e.get(); idIndex[e->id] = e.get(); }
    startIndexBuild();
    saveSoon();
    changed();
    return (int) (before - list.size());
}

void Archive::startScan (bool full)
{
    if (scanner != nullptr || folders.isEmpty()) return;
    total = 0; done = 0;
    seenPaths.clear();
    scanStart = juce::Time::getMillisecondCounter();
    scanner = std::make_unique<Scanner> (*this, folders, full);
    scanner->startThread (juce::Thread::Priority::low);
    startTimer (150);
    changed();
}

void Archive::stopScan()
{
    if (scanner == nullptr) return;
    scanner->signalThreadShouldExit();
    scanner->stopThread (20000);
    scanner.reset();
    pump();
    save();
    changed();
}

juce::String Archive::scanStatus() const
{
    if (scanner == nullptr) return {};
    if (scanner->phase == 0) return T ("Suche MIDI-Dateien … %1 gefunden", total.load());
    const double secs = (juce::Time::getMillisecondCounter() - scanStart) / 1000.0;
    return T ("Lese ein: %1 / %2  (%3)", done.load(), total.load(), formatDuration (secs));
}

void Archive::insertOrReplace (std::unique_ptr<Entry> e, bool& replaced)
{
    if (auto* old = byPath (e->path))
    {
        e->id = old->id; e->rating = old->rating; e->favourite = old->favourite;
        *old = std::move (*e);
        reindex (*old);
        replaced = true;
        return;
    }
    e->id = nextId++;
    auto* raw = e.get();
    pathIndex[raw->path.toLowerCase()] = raw;
    idIndex[raw->id] = raw;
    list.push_back (std::move (e));
    if (index != nullptr) index->add (raw->id, fingerprintOf (*raw));
}

void Archive::pump()
{
    if (index == nullptr) installIndex();
    std::deque<std::unique_ptr<Entry>> batch;
    {
        const juce::ScopedLock sl (pendingLock);
        batch.swap (pending);
    }
    bool replaced = false;
    for (auto& e : batch) insertOrReplace (std::move (e), replaced);
    juce::ignoreUnused (replaced);
    if (! batch.empty()) finishGroups();

    const bool finished = scanner != nullptr && scanner->finished && ! scanner->isThreadRunning();
    if (finished)
    {
        // whatever was not seen in the scanned folders is missing now
        std::unordered_set<juce::String> seen;
        {
            const juce::ScopedLock sl (pendingLock);
            for (auto& s : seenPaths) seen.insert (s);
        }
        for (auto& e : list)
        {
            bool inFolders = false;
            for (auto& f : folders) inFolders |= e->path.startsWithIgnoreCase (f + juce::File::getSeparatorString());
            if (inFolders)
            {
                if (seen.count (e->path.toLowerCase()) == 0) e->flags |= fMissing;
                else e->flags &= ~(juce::uint32) fMissing;
            }
        }
        scanner.reset();
        save();
    }
    if (! batch.empty() || finished) { dirty = dirty || ! batch.empty(); changed(); }
}

void Archive::waitForScan()
{
    while (scanner != nullptr)
    {
        juce::Thread::sleep (50);
        pump();
    }
}

void Archive::timerCallback()
{
    if (index == nullptr && indexReady) installIndex();
    if (scanner != nullptr)
    {
        pump();
        if (scanner != nullptr)
        {
            sendChangeMessage();
            if (dirty && ++saveCountdown >= 600) { saveCountdown = 0; save(); }   // ~90 s: long scans must not lose everything
        }
        return;
    }
    if (getTimerInterval() != 1000) startTimer (1000);
    if (dirty && ++saveCountdown >= 3) { saveCountdown = 0; save(); }
}

// ============================================================================================ edits
bool Archive::setField (Entry& e, Field f, const juce::String& value, juce::String& error)
{
    UndoGroup g (*this, e.name);
    remember (e, true);
    if (! writeMidiField (juce::File (e.path), f, value, error)) return false;
    reread (e);
    return true;
}

void Archive::openUndo (const juce::String& label)
{
    undoStack.push_back ({ label, {} });
    undoOpen = true;
}

void Archive::closeUndo()
{
    undoOpen = false;
    if (! undoStack.empty() && undoStack.back().items.empty()) undoStack.pop_back();
    size_t bytes = 0;   // keep the last 100 steps, at most ~64 MB of file copies
    for (auto& st : undoStack) for (auto& it : st.items) bytes += it.data.getSize();
    while (undoStack.size() > 100 || (bytes > ((size_t) 64 << 20) && undoStack.size() > 1))
    {
        for (auto& it : undoStack.front().items) bytes -= it.data.getSize();
        undoStack.erase (undoStack.begin());
    }
}

void Archive::remember (Entry& e, bool file)
{
    if (! undoOpen || undoStack.empty()) return;
    auto& items = undoStack.back().items;
    for (auto& it : items)
        if (it.id == e.id && (it.file || ! file)) return;   // the state before the first change of this step counts
    UndoItem it;
    it.id = e.id;
    it.path = e.path;
    it.rating = e.rating;
    it.favourite = e.favourite;
    if (file)
    {
        if (! juce::File (e.path).loadFileAsData (it.data)) return;
        it.file = true;
    }
    items.push_back (std::move (it));
}

bool Archive::undo (juce::String& report)
{
    if (undoStack.empty()) return false;
    auto step = std::move (undoStack.back());
    undoStack.pop_back();
    int restored = 0;
    juce::StringArray failed;
    for (auto it = step.items.rbegin(); it != step.items.rend(); ++it)
    {
        auto* e = byId (it->id);
        if (it->file)
        {
            const juce::File f (e != nullptr ? e->path : it->path);
            if (! f.replaceWithData (it->data.getData(), it->data.getSize())) { failed.add (f.getFileName()); continue; }
            if (e != nullptr) reread (*e);
        }
        else if (e != nullptr) { e->rating = it->rating; e->favourite = it->favourite; }
        ++restored;
    }
    saveSoon();
    changed();
    report = failed.isEmpty() ? juce::String() : failed.joinIntoString (", ");
    return restored > 0;
}

bool Archive::reread (Entry& e)
{
    juce::File f (e.path);
    juce::MemoryBlock mb;
    if (! f.loadFileAsData (mb)) return false;
    e.size = f.getSize();
    e.mtime = f.getLastModificationTime().toMilliseconds();
    const auto oldCrc = e.crc;
    e.crc = crc32 (mb.getData(), mb.getSize());
    e.fill (analyseMidi (mb));
    e.flags &= ~(juce::uint32) fMissing;
    if (oldCrc != e.crc) reindex (e);
    saveSoon();
    changed();
    return true;
}

void Archive::removeEntries (const std::vector<int>& ids)
{
    std::unordered_set<int> drop (ids.begin(), ids.end());
    list.erase (std::remove_if (list.begin(), list.end(), [&] (auto& e) { return drop.count (e->id) != 0; }), list.end());
    pathIndex.clear(); idIndex.clear();
    for (auto& e : list) { pathIndex[e->path.toLowerCase()] = e.get(); idIndex[e->id] = e.get(); }
    if (index != nullptr && ids.size() <= 50)
    {
        for (int id : ids) index->remove (id);
        finishGroups();
    }
    else startIndexBuild();
    saveSoon();
    changed();
}

void Archive::relocate (Entry& e, const juce::File& f)
{
    pathIndex.erase (e.path.toLowerCase());
    e.path = f.getFullPathName();
    e.name = f.getFileName();
    e.folder = f.getParentDirectory().getFullPathName();
    e.mtime = f.getLastModificationTime().toMilliseconds();
    e.flags &= ~(juce::uint32) fMissing;
    pathIndex[e.path.toLowerCase()] = &e;
    saveSoon();
    changed();
}

Entry* Archive::addFile (const juce::File& f)
{
    auto e = std::make_unique<Entry>();
    e->path = f.getFullPathName(); e->name = f.getFileName(); e->folder = f.getParentDirectory().getFullPathName();
    e->size = f.getSize(); e->mtime = f.getLastModificationTime().toMilliseconds();
    juce::MemoryBlock mb;
    if (! f.loadFileAsData (mb)) return nullptr;
    e->crc = crc32 (mb.getData(), mb.getSize());
    e->fill (analyseMidi (mb));
    const auto path = e->path;
    bool replaced = false;
    insertOrReplace (std::move (e), replaced);
    finishGroups();
    saveSoon();
    changed();
    return byPath (path);
}

bool Archive::isInsideFolders (const juce::File& f) const
{
    for (auto& d : folders) if (f.isAChildOf (juce::File (d))) return true;
    return false;
}

void Archive::setRating (Entry& e, int r) { UndoGroup g (*this, e.name); remember (e, false); e.rating = juce::jlimit (0, 5, r); saveSoon(); changed(); }
void Archive::setFavourite (Entry& e, bool fav) { UndoGroup g (*this, e.name); remember (e, false); e.favourite = fav; saveSoon(); changed(); }

void Archive::markNotDuplicate (int a, int b)
{
    if (a == b) return;
    waitForIndex();     // the graph must be current before cutting
    std::vector<juce::uint64> pairs { DupIndex::pairKey (a, b) };
    if (index != nullptr)
    {
        for (auto k : index->separate (a, b)) pairs.push_back (k);
        for (auto k : pairs) index->notDup.insert (k);
        index->cut (pairs);
    }
    for (auto k : pairs) notDup.insert (k);
    finishGroups();
    saveSoon();
    changed();
}

void Archive::setSimilarityThreshold (float t)
{
    settings.setProperty ("simThreshold", (double) juce::jlimit (0.1f, 1.0f, t), nullptr);
    startIndexBuild();
    saveSoon();
    changed();
}

// ============================================================================================ duplicate index

int DupIndex::find (int id)
{
    auto it = parent.find (id);
    if (it == parent.end()) { parent[id] = id; return id; }
    int r = id;
    while (parent[r] != r) r = parent[r];
    while (parent[id] != r) { const int nx = parent[id]; parent[id] = r; id = nx; }
    return r;
}

void DupIndex::unite (int a, int b)
{
    a = find (a); b = find (b);
    if (a == b) return;
    if (a < b) parent[b] = a; else parent[a] = b;
}

void DupIndex::add (int id, const FP& fp)
{
    fps[id] = fp;
    if (fp.error) return;
    auto link = [&] (int other, float score, juce::uint8 kind)
    {
        if (other == id || notDup.count (pairKey (other, id)) != 0) return;
        auto put = [] (std::vector<DupLink>& v, int o, float s, juce::uint8 k)
        {
            for (auto& l : v) if (l.other == o) { if (k > l.kind || (k == l.kind && s > l.score)) { l.score = s; l.kind = k; } return; }
            v.push_back ({ o, s, k });
        };
        put (links[id], other, score, kind);
        put (links[other], id, score, kind);
        unite (id, other);
    };
    auto& c = crcIdx[fp.crc];
    for (int o : c) link (o, 1.0f, dupIdentical);
    c.push_back (id);
    if (fp.noteHash != 0 && fp.notes >= 8)
    {
        auto& v = noteIdx[fp.noteHash];
        for (int o : v) if (fps[o].crc != fp.crc) link (o, 1.0f, dupSameNotes);
        v.push_back (id);
    }
    if (fp.gramCount >= 20 && ! fp.sketch.empty())
    {
        shared.clear();
        for (auto g : fp.sketch)
        {
            auto& post = gramIdx[g];
            if (post.size() < 400)
                for (int o : post) ++shared[o];
            post.push_back (id);
        }
        for (auto& [o, cnt] : shared)
        {
            if (cnt < 4) continue;
            auto& ofp = fps[o];
            if (ofp.gramCount < 20 || ofp.noteHash == fp.noteHash) continue;
            const float s = sketchSimilarity (fp.sketch, ofp.sketch);
            if (s >= threshold) link (o, s, dupSimilar);
        }
    }
}

void DupIndex::remove (int id)
{
    auto it = fps.find (id);
    if (it == fps.end()) return;
    auto drop = [id] (std::vector<int>& v) { v.erase (std::remove (v.begin(), v.end(), id), v.end()); };
    if (auto c = crcIdx.find (it->second.crc); c != crcIdx.end()) drop (c->second);
    if (auto n = noteIdx.find (it->second.noteHash); n != noteIdx.end()) drop (n->second);
    for (auto g : it->second.sketch) if (auto p = gramIdx.find (g); p != gramIdx.end()) drop (p->second);
    if (auto l = links.find (id); l != links.end())
    {
        for (auto& x : l->second)
            if (auto o = links.find (x.other); o != links.end())
                o->second.erase (std::remove_if (o->second.begin(), o->second.end(), [id] (auto& y) { return y.other == id; }), o->second.end());
        links.erase (l);
    }
    fps.erase (it);
    parent.clear();
    for (auto& [a, v] : links) for (auto& x : v) unite (a, x.other);
}

// Minimum cut between a and b (Edmonds-Karp). Strong links cost more to cut than weak "similar" ones,
// so the user's "not a duplicate" removes as little as possible from the group.
std::vector<juce::uint64> DupIndex::separate (int a, int b)
{
    if (find (a) != find (b)) return {};
    const int root = find (a);
    std::unordered_map<int, int> node;
    std::vector<int> ids;
    for (auto& [id, v] : links)
        if (! v.empty() && find (id) == root) { node[id] = (int) ids.size(); ids.push_back (id); }
    const int n = (int) ids.size();
    struct Edge { int to; double cap; };
    std::vector<Edge> edges;
    std::vector<std::vector<int>> adj ((size_t) n);
    auto weight = [] (const DupLink& l) { return l.kind == dupIdentical ? 1000.0 : l.kind == dupSameNotes ? 100.0 : 1.0 + l.score; };
    for (int i = 0; i < n; ++i)
        for (auto& l : links[ids[(size_t) i]])
        {
            auto it = node.find (l.other);
            if (it == node.end() || it->second < i) continue;   // each undirected link once
            const double w = weight (l);
            adj[(size_t) i].push_back ((int) edges.size()); edges.push_back ({ it->second, w });
            adj[(size_t) it->second].push_back ((int) edges.size()); edges.push_back ({ i, w });
        }
    const int s = node[a], t = node[b];
    std::vector<int> prevEdge ((size_t) n);
    auto bfs = [&]
    {
        std::fill (prevEdge.begin(), prevEdge.end(), -1);
        std::vector<char> seen ((size_t) n, 0);
        std::vector<int> q { s };
        seen[(size_t) s] = 1;
        for (size_t qi = 0; qi < q.size(); ++qi)
            for (int ei : adj[(size_t) q[qi]])
            {
                auto& e = edges[(size_t) ei];
                if (e.cap <= 1e-9 || seen[(size_t) e.to]) continue;
                seen[(size_t) e.to] = 1; prevEdge[(size_t) e.to] = ei; q.push_back (e.to);
            }
        return seen;
    };
    for (int guard = 0; guard < 100000; ++guard)
    {
        bfs();
        if (prevEdge[(size_t) t] < 0) break;
        double f = 1e18;
        for (int v = t; v != s; v = edges[(size_t) (prevEdge[(size_t) v] ^ 1)].to) f = std::min (f, edges[(size_t) prevEdge[(size_t) v]].cap);
        for (int v = t; v != s; v = edges[(size_t) (prevEdge[(size_t) v] ^ 1)].to)
        {
            edges[(size_t) prevEdge[(size_t) v]].cap -= f;
            edges[(size_t) (prevEdge[(size_t) v] ^ 1)].cap += f;
        }
    }
    const auto side = bfs();
    std::vector<juce::uint64> out;
    for (int i = 0; i < n; ++i)
        if (side[(size_t) i])
            for (auto& l : links[ids[(size_t) i]])
                if (auto it = node.find (l.other); it != node.end() && ! side[(size_t) it->second])
                    out.push_back (pairKey (ids[(size_t) i], l.other));
    return out;
}

void DupIndex::cut (const std::vector<juce::uint64>& pairs)
{
    if (pairs.empty()) return;
    const std::set<juce::uint64> drop (pairs.begin(), pairs.end());
    for (auto& [id, v] : links)
        v.erase (std::remove_if (v.begin(), v.end(), [&, me = id] (const DupLink& l) { return drop.count (pairKey (me, l.other)) != 0; }), v.end());
    parent.clear();
    for (auto& [x, v] : links) for (auto& l : v) unite (x, l.other);
}

void Archive::startIndexBuild()
{
    if (indexThread.joinable()) indexThread.join();
    index.reset();
    indexReady = false;
    std::vector<std::pair<int, DupIndex::FP>> snap;
    snap.reserve (list.size());
    for (auto& e : list) snap.push_back ({ e->id, fingerprintOf (*e) });
    const float thr = similarityThreshold();
    auto nd = notDup;
    indexThread = std::thread ([this, snap = std::move (snap), thr, nd] {
        auto idx = std::make_unique<DupIndex>();
        idx->threshold = thr;
        idx->notDup = nd;
        idx->fps.reserve (snap.size());
        for (auto& [id, fp] : snap) idx->add (id, fp);
        const juce::ScopedLock sl (builtLock);
        builtIndex = std::move (idx);
        indexReady = true;
    });
}

bool Archive::installIndex()
{
    if (index != nullptr) return true;
    if (! indexReady) return false;
    if (indexThread.joinable()) indexThread.join();
    {
        const juce::ScopedLock sl (builtLock);
        index = std::move (builtIndex);
    }
    // entries changed while the index was built
    for (int id : replacedIds) if (auto* e = byId (id)) { index->remove (id); index->add (id, fingerprintOf (*e)); }
    replacedIds.clear();
    for (auto& e : list) if (index->fps.find (e->id) == index->fps.end()) index->add (e->id, fingerprintOf (*e));
    finishGroups();
    changed();
    return true;
}

void Archive::waitForIndex()
{
    if (indexThread.joinable()) indexThread.join();
    installIndex();
}

void Archive::reindex (Entry& e)
{
    if (index == nullptr) { replacedIds.push_back (e.id); return; }
    index->remove (e.id);
    index->add (e.id, fingerprintOf (e));
    finishGroups();
}

void Archive::finishGroups()
{
    if (index == nullptr) return;
    std::unordered_map<int, int> sizes;
    for (auto& e : list) if (index->links.count (e->id)) ++sizes[index->find (e->id)];
    for (auto& e : list)
    {
        auto it = index->links.find (e->id);
        if (it == index->links.end() || it->second.empty()) { e->group = 0; e->groupSize = 0; e->bestMatch = 0; e->bestScore = 0; e->dupKind = dupNone; continue; }
        e->group = index->find (e->id);
        e->groupSize = sizes[e->group];
        const DupLink* best = nullptr;
        for (auto& l : it->second)
            if (best == nullptr || l.kind > best->kind || (l.kind == best->kind && l.score > best->score)) best = &l;
        e->bestMatch = best->other; e->bestScore = best->score; e->dupKind = best->kind;
    }
}

void Archive::rebuildDuplicates()
{
    if (indexThread.joinable()) indexThread.join();
    auto idx = std::make_unique<DupIndex>();
    idx->threshold = similarityThreshold();
    idx->notDup = notDup;
    for (auto& e : list) idx->add (e->id, fingerprintOf (*e));
    index = std::move (idx);
    indexReady = true;
    replacedIds.clear();
    finishGroups();
}

std::vector<int> Archive::groupMembers (int group) const
{
    std::vector<int> out;
    if (group == 0) return out;
    for (auto& e : list) if (e->group == group) out.push_back (e->id);
    return out;
}

std::vector<DupLink> Archive::linksOf (int id) const
{
    if (index == nullptr) return {};
    auto it = index->links.find (id);
    return it != index->links.end() ? it->second : std::vector<DupLink>();
}

int Archive::duplicateGroups() const
{
    std::unordered_set<int> g;
    for (auto& e : list) if (e->group != 0) g.insert (e->group);
    return (int) g.size();
}

// ============================================================================================ persistence
juce::File Archive::databaseFile() const
{
    if (auto env = juce::SystemStats::getEnvironmentVariable ("MIDIARCHIV_DB", {}); env.isNotEmpty()) return juce::File (env);
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("MIDIARCHIV").getChildFile ("archiv.mdb");
}

namespace
{
constexpr int kMagic = 0x4352414d;   // "MARC"
constexpr int kVersion = 2;
}

void Archive::save()
{
    dirty = false;
    const auto file = databaseFile();
    file.getParentDirectory().createDirectory();
    juce::TemporaryFile tmp (file);
    {
        juce::FileOutputStream os (tmp.getFile(), 1 << 20);
        if (! os.openedOk()) return;
        os.writeInt (kMagic); os.writeInt (kVersion);
        os.writeInt (nextId);
        os.writeInt (folders.size());
        for (auto& f : folders) os.writeString (f);
        {
            juce::MemoryOutputStream ms;
            settings.writeToStream (ms);
            os.writeInt ((int) ms.getDataSize());
            os.write (ms.getData(), ms.getDataSize());
        }
        os.writeInt ((int) notDup.size());
        for (auto k : notDup) os.writeInt64 ((juce::int64) k);
        os.writeInt ((int) list.size());
        for (auto& e : list)
        {
            os.writeInt (e->id);
            for (auto* s : { &e->path, &e->title, &e->artist, &e->composer, &e->genre, &e->copyright, &e->comment, &e->language, &e->lyrics,
                             &e->key, &e->keyMeta, &e->scale, &e->timeSig, &e->instruments, &e->error })
                os.writeString (*s);
            os.writeInt64 (e->size); os.writeInt64 (e->mtime);
            os.writeInt ((int) e->crc); os.writeInt ((int) e->flags);
            for (int v : { e->year, e->format, e->tracks, e->channels, e->notes, e->drumNotes, e->rating, e->favourite ? 1 : 0, e->gramCount })
                os.writeInt (v);
            os.writeFloat (e->bpm); os.writeFloat (e->seconds); os.writeFloat (e->keyConfidence);
            os.writeInt64 ((juce::int64) e->noteHash);
            os.writeByte ((char) e->sketch.size());
            for (auto g : e->sketch) os.writeInt ((int) g);
            os.write (e->mountain.data(), e->mountain.size());
            os.write (e->pitchClasses.data(), e->pitchClasses.size());
            os.writeInt (e->group); os.writeInt (e->groupSize); os.writeInt (e->bestMatch); os.writeFloat (e->bestScore); os.writeByte ((char) e->dupKind);
        }
        os.flush();
        if (os.getStatus().failed()) return;
    }
    tmp.overwriteTargetFileWithTemporary();
}

bool Archive::load()
{
    const auto file = databaseFile();
    juce::FileInputStream raw (file);
    if (! raw.openedOk()) return false;
    juce::BufferedInputStream is (raw, 1 << 20);
    if (is.readInt() != kMagic) return false;
    const int fileVersion = is.readInt();
    if (fileVersion < 1 || fileVersion > kVersion) return false;
    nextId = is.readInt();
    folders.clear();
    for (int i = is.readInt(); --i >= 0;) folders.add (is.readString());
    {
        const int n = is.readInt();
        juce::MemoryBlock mb;
        is.readIntoMemoryBlock (mb, n);
        auto t = juce::ValueTree::readFromData (mb.getData(), mb.getSize());
        if (t.isValid()) settings = t;
    }
    for (int i = is.readInt(); --i >= 0;) notDup.insert ((juce::uint64) is.readInt64());
    const int count = is.readInt();
    list.reserve ((size_t) juce::jmax (0, count));
    for (int i = 0; i < count && ! is.isExhausted(); ++i)
    {
        auto e = std::make_unique<Entry>();
        e->id = is.readInt();
        for (auto* s : { &e->path, &e->title, &e->artist, &e->composer, &e->genre, &e->copyright, &e->comment, &e->language, &e->lyrics,
                         &e->key, &e->keyMeta, &e->scale, &e->timeSig, &e->instruments, &e->error })
            *s = is.readString();
        e->size = is.readInt64(); e->mtime = is.readInt64();
        e->crc = (juce::uint32) is.readInt(); e->flags = (juce::uint32) is.readInt();
        for (int* v : { &e->year, &e->format, &e->tracks, &e->channels, &e->notes, &e->drumNotes, &e->rating })
            *v = is.readInt();
        e->favourite = is.readInt() != 0;
        e->gramCount = is.readInt();
        e->bpm = is.readFloat(); e->seconds = is.readFloat(); e->keyConfidence = is.readFloat();
        e->noteHash = (juce::uint64) is.readInt64();
        const int ns = (juce::uint8) is.readByte();
        e->sketch.resize ((size_t) ns);
        for (auto& g : e->sketch) g = (juce::uint32) is.readInt();
        is.read (e->mountain.data(), (int) e->mountain.size());
        is.read (e->pitchClasses.data(), (int) e->pitchClasses.size());
        if (fileVersion >= 2)
        {
            e->group = is.readInt(); e->groupSize = is.readInt(); e->bestMatch = is.readInt(); e->bestScore = is.readFloat(); e->dupKind = (juce::uint8) is.readByte();
        }
        const juce::File f (e->path);
        e->name = f.getFileName();
        e->folder = f.getParentDirectory().getFullPathName();
        pathIndex[e->path.toLowerCase()] = e.get();
        idIndex[e->id] = e.get();
        nextId = juce::jmax (nextId, e->id + 1);
        list.push_back (std::move (e));
    }
    return true;
}
} // namespace ma
