#include "pch.h"
#include "jName.h"

jName::Storage& jName::GetStorage()
{
    // Shader registration can intern names before another translation unit's
    // globals are initialized. Construct the maps and their lock together on
    // first use. Cached character pointers must also survive static teardown.
    static Storage* const storage = new Storage;
    return *storage;
}

const jName jName::Invalid;
