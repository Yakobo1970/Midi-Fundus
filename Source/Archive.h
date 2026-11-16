// MIDIARCHIV – the archive database: folders, scanned entries, duplicate groups. Shared by all instances in a process.
#pragma once
#include <functional>
#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>
#include "MidiData.h"
#include <unordered_map>
#include <set>
#include <deque>
#include <thread>

namespace ma
{
enum Flags : juce::uint32
{
    fTitleFromName = 1, fArtistFromName = 2, fMissing = 4, fError = 8, fKaraoke = 16, fRmi = 32, fTempoChanges = 64, fUltraStar = 128, fConverted = 256,
    fProtected = 512   // inside a write-protected folder (not stored, recomputed)
};

enum DupKind : juce::uint8 { dupNone = 0, dupSimilar = 1, dupSameNotes = 2, dupIdentical = 3 };

struct Entry
{
    int id = 0;
    juce::String path, name, folder;
    juce::int64 size = 0, mtime = 0;
    juce::uint32 crc = 0, flags = 0;
    juce::String title, artist, composer, genre, copyright, comment, language, lyrics;
    juce::String key, keyMeta, scale, timeSig, instruments, error;
    int year = 0, format = 1, tracks = 0, channels = 0, notes = 0, drumNotes = 0, rating = 0;
    bool favourite = false;
    float bpm = 0, seconds = 0, keyConfidence = 0;
    juce::uint64 noteHash = 0;
    std::vector<juce::uint32> sketch;
    int gramCount = 0;
    std::array<juce::uint8, kMountBins * kMountTracks> mountain {};
    std::array<juce::uint8, 12> pitchClasses {};

    // duplicate state (runtime)
    int group = 0;          // id of the group's first entry, 0 = no duplicate
    int groupSize = 0;
    int bestMatch = 0;      // id of the most similar other entry
    float bestScore = 0;
    juce::uint8 dupKind = dupNone;

    bool has (Flags f) const { return (flags & f) != 0; }
    void fill (const MidiInfo& m);   // copies analysis results, applies file-name fallbacks
};

struct DupLink { int other; float score; juce::uint8 kind; };
struct DupIndex;

class Archive : public juce::ChangeBroadcaster, private juce::Timer
{
public:
    Archive();
    ~Archive() override;

    // ---- data (message thread only)
    const std::vector<std::unique_ptr<Entry>>& entries() const { return list; }
    Entry* byId (int id) const;
    Entry* byPath (const juce::String& path) const;
    juce::uint32 version() const { return changeCounter; }

    juce::StringArray folders;
    juce::ValueTree settings { "SETTINGS" };   // UI layout, theme, thresholds (shared by standalone and plugin)

    juce::String addFolder (const juce::File& dir);   // returns the archive folder that now covers dir
    // Write protection per folder: Midi-Fundus never writes, moves or deletes files there (settings "protected").
    juce::StringArray protectedFolders() const;
    bool isProtected (const juce::String& path) const;
    void setProtected (const juce::String& folder, bool on);
    juce::String protectedMessage() const;
    // Takes a folder out of the archive together with its entries (a running scan is stopped first,
    // otherwise it would put files of the folder back). Returns the number of removed entries.
    int removeFolder (const juce::String& path);
    int countInFolder (const juce::String& path) const;
    int removeMissing();
    // Entries that belong to no archive folder any more (e.g. after removing a folder with older versions).
    int countOutsideFolders() const;
    int removeOutsideFolders();

    // ---- scanning
    void startScan (bool fullRescan = false, const juce::StringArray& only = {});   // only: subset of folders (empty = all)
    void stopScan();
    bool isScanning() const { return scanner != nullptr; }
    juce::String scanStatus() const;
    int scanTotal() const { return total.load(); }
    int scanDone() const { return done.load(); }
    void pump();                   // integrates scan results (called by the timer; tools may call it directly)
    void waitForScan();            // tools: blocks and pumps until finished

    // ---- edits
    bool setField (Entry& e, Field f, const juce::String& value, juce::String& error);   // writes into the file
    void setRating (Entry& e, int rating);
    void setFavourite (Entry& e, bool fav);
    bool reread (Entry& e);

    // ---- undo (message thread): file contents before a write, rating/favourite before a change
    struct UndoGroup   // everything changed while it lives is undone in one step
    {
        UndoGroup (Archive& a, const juce::String& label) : arch (a) { if (arch.undoDepth++ == 0) arch.openUndo (label); }
        ~UndoGroup() { if (--arch.undoDepth == 0) arch.closeUndo(); }
        Archive& arch;
    };
    bool canUndo() const { return ! undoStack.empty(); }
    juce::String undoLabel() const { return undoStack.empty() ? juce::String() : undoStack.back().label; }
    bool undo (juce::String& report);   // restores the last step; false if nothing to undo

    // ---- file operations (message thread)
    void removeEntries (const std::vector<int>& ids);              // database only
    void relocate (Entry& e, const juce::File& newFile);           // after a move/rename: keeps id, rating, favourite
    Entry* addFile (const juce::File& f);                          // analyse now (e.g. after copying into the archive)
    bool isInsideFolders (const juce::File& f) const;
    void markNotDuplicate (int idA, int idB);

    // ---- duplicates
    std::vector<int> groupMembers (int group) const;
    std::vector<DupLink> linksOf (int id) const;
    float similarityThreshold() const { return (float) (double) settings.getProperty ("simThreshold", 0.45); }
    void setSimilarityThreshold (float t);
    void rebuildDuplicates();      // synchronous
    int duplicateGroups() const;
    bool indexing() const { return index == nullptr; }
    void waitForIndex();

    // ---- persistence
    juce::File databaseFile() const;
    void save();
    void saveSoon() { dirty = true; }
    bool load();

private:
    struct Scanner;
    std::vector<std::unique_ptr<Entry>> list;
    std::unordered_map<juce::String, Entry*> pathIndex;
    std::unordered_map<int, Entry*> idIndex;
    int nextId = 1;
    juce::uint32 changeCounter = 1;
    bool dirty = false;
    int saveCountdown = 0;

    // scanning
    std::unique_ptr<Scanner> scanner;
    juce::CriticalSection pendingLock;
    std::deque<std::unique_ptr<Entry>> pending;
    juce::StringArray seenPaths;
    std::atomic<int> total { 0 }, done { 0 };
    juce::String scanPhase;
    juce::uint32 scanStart = 0;

    // duplicate index (built in the background at start-up; null until ready)
    std::unique_ptr<DupIndex> index;
    std::unique_ptr<DupIndex> builtIndex;
    std::thread indexThread;
    std::atomic<bool> indexReady { false };
    juce::CriticalSection builtLock;
    std::set<juce::uint64> notDup;
    void startIndexBuild();
    bool installIndex();          // message thread: takes a finished background index
    void finishGroups();
    void reindex (Entry& e);
    std::vector<int> replacedIds;

    struct UndoItem { int id = 0; juce::String path; bool file = false; juce::MemoryBlock data; int rating = 0; bool favourite = false; };
    struct UndoStep { juce::String label; std::vector<UndoItem> items; };
    std::vector<UndoStep> undoStack;
    int undoDepth = 0;
    bool undoOpen = false;
    void openUndo (const juce::String& label);
    void closeUndo();
    void remember (Entry& e, bool file);   // before a change

    int removeWhere (const std::function<bool (const Entry&)>& pred);
    void applyProtection();

    void markProtection (Entry& e, const juce::StringArray& prot) const;

    void insertOrReplace (std::unique_ptr<Entry> e, bool& replaced);
    void changed() { ++changeCounter; sendChangeMessage(); }
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Archive)
};

bool isMidiFileName (const juce::String& name);
bool isArchiveFile (const juce::File& f);   // MIDI by name, or an UltraStar .txt (header check)
juce::String formatDuration (double seconds);
} // namespace ma
