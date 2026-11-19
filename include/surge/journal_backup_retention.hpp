#pragma once
#include "surge/event_journal.hpp"
#include "surge/snapshot_store.hpp"
#include <string>

namespace surge {

// Deletes the retained full journal generation only after proving that the
// active compacted journal and both snapshot generations can recover from the
// same checkpoint. This is an explicit, irreversible retention operation.
bool prune_verified_journal_backup(const std::string& journal_path,
                                   EventJournal& active_journal,
                                   SnapshotStore& snapshot_store,
                                   std::string* error);

}  // namespace surge
