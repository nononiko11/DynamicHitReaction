#pragma once
namespace dhf { namespace log {
void Init(const char* path, bool enabled);
void Write(const char* fmt, ...);
// Writes at most `maxLines` lines for the given channel (0..7) so a busy game cannot flood the log.
void WriteLimited(int channel, int maxLines, const char* fmt, ...);
}}
