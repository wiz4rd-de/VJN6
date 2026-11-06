// Patch and bank files (SPEC §4.3, §4.6, R-D3, R-D4). Pure functions; safe on any thread.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "vjn6/types.h"

namespace vjn6 {
struct Bulk;

namespace files {

// §4.3: NFC; every character outside 0x20-0x7E -> '?'; truncate to 12; strip trailing spaces.
std::string normaliseName(const std::string& utf8);
Name toName(const std::string& utf8);

enum class Result {
  PatchLoad,   // .vjn6 or a one-message .syx: edit buffer (Manual)
  BankLoad,    // .vjn6bank: replaces all slots
  SyxImport,   // two or more 30/31 messages: bank import
  Malformed,   // unknown, malformed, empty or over 1 MiB
};

// Parses a file by its extension (.vjn6, .vjn6bank, .syx; case-insensitive) into a Bulk.
Result parse(const uint8_t* data, std::size_t len, const std::string& fileName, Bulk& out);

// Canonical writers (R-D3): UTF-8, 2-space indent, keys in §4.6/§4.1 order, LF, final newline.
std::string patchJson(const Patch& p, const Name& n);
std::string bankJson(const Slot* bank);
std::vector<uint8_t> patchSyx(const Patch& p);   // one canonical F0 41 31, n = 0, pp = 0
std::vector<uint8_t> bankSyx(const Slot* bank);  // 128 canonical F0 41 30, n = 0, pp = 0..127

// Export file names (§4.6).
std::string patchFileName(const Name& n, const char* ext);  // e.g. "BRASS 1.vjn6"
inline const char* bankFileNameJson() { return "VJN6 bank.vjn6bank"; }
inline const char* bankFileNameSyx() { return "VJN6 bank.syx"; }

// Middle-shortening with an ellipsis for notices (§5.5): longer than max characters -> "abc…xyz".
std::string shortenMiddle(const std::string& s, std::size_t max);

}  // namespace files
}  // namespace vjn6
