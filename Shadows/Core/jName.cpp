#include "pch.h"
#include "jName.h"

std::unordered_map<uint32, std::shared_ptr<std::string>> jName::s_NameTable;
const jName jName::Invalid;