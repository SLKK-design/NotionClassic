/* rich_text_json.h — Notion rich_text and block JSON serialisation. */
#pragma once

#include <string>
#include "blocks.h"

/* Single source of truth for Notion block JSON bodies.
   richText: pre-built "\"rich_text\":..." string, or NULL for empty placeholder.
   out is overwritten (not appended). */
void build_block_json(BlockType type, const char* richText,
                      bool checked, std::string& out);

/* Wrap a block JSON object in a Notion /children append request.
   Uses position API: atStart → position:start, afterId → position:after_block. */
void build_children_body(const std::string& blockJson,
                         const char*        afterId,
                         bool               atStart,
                         std::string&       out);

/* One-pass builders for save bodies (append to out, nothing temporary):
   RichJsonEstimate — capacity to reserve for n pieces;
   AppendRichArray  — the pieces' rich_text array;
   AppendBlockJson  — the whole block object for `type` (n == 0: empty text);
   AppendChildrenBody — a /children append request with that block.      */
size_t RichJsonEstimate(const Block* pieces, int n);
void   AppendRichArray(std::string& out, const Block* pieces, int n);
void   AppendBlockJson(BlockType type, bool checked, const Block* pieces, int n, std::string& out);
void   AppendChildrenBody(BlockType type, bool checked, const Block* pieces, int n,
                          const char* afterId, bool atStart, std::string& out);
