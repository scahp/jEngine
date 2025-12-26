#include "pch.h"
#include "jName.h"

robin_hood::unordered_map<uint32, std::shared_ptr<std::string>> jName::s_NameTable;
robin_hood::unordered_map<uint32, std::shared_ptr<std::wstring>> jName::s_WideNameTable;
jMutexRWLock jName::Lock;
const jName jName::Invalid;