#pragma once

#include <ArduinoJson.h>

#include <string>

#include "Program.h"

// Serialization and revision of Programs. Parsing with validation lives in
// ProgramValidator so there is one interpretation of Program JSON.

namespace acorn {
namespace ProgramJson {

// Normalized form: known fields only, times as HH:MM:SS.
void toJson(const Program& p, JsonObject out);
void eventToJson(const Event& e, JsonObject out);
std::string toString(const Program& p, bool pretty = false);

// Canonical serialization: object keys sorted, no whitespace.
std::string canonical(JsonVariantConst v);

// 8 lowercase hex chars: FNV-1a over canonical({schema_version, events})
// of the normalized Program. Independent of name/description/modified_utc.
std::string revision(const Program& p);

// Revision of un-normalized Program JSON exactly as given (mock
// programRevision semantics). Used for drafts that fail validation; for a
// valid Program prefer revision(), which hashes the normalized form.
std::string rawRevision(JsonVariantConst p);

}  // namespace ProgramJson
}  // namespace acorn
