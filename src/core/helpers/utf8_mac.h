/* utf8_mac.h — JSON text writers (Mac Roman + emoji back to UTF-8) on top
   of the fixed-array conversion in utf8_mac_lite.h; files that only
   decode include that instead.  */
#pragma once

#include <string.h>
#include <string>

#include "utf8_mac_lite.h"  /* StripVariationSelectors, utf8_to_macroman_into */

/* Escape a UTF-8 string for embedding in a JSON string literal.        */
std::string json_escape(const std::string& s);

/* Append Mac Roman text as JSON-escaped UTF-8 in one pass.  Each sentinel takes emojiSeqs[emojiIdx++] while there are
   any (emojiIdx carries on across slices of one block).                */
void append_json_text(std::string& out, const char* mac, size_t len,
                      const char emojiSeqs[][32], uint8_t emojiCount, uint8_t& emojiIdx);
