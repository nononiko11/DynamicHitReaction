#include "Log.h"
#include <cstdio>
#include <cstdarg>
#include <ctime>

namespace dhf { namespace log {
namespace {
FILE* g_file = nullptr;
int g_counts[8] = {};

void VWrite(const char* fmt, va_list ap) {
    if (!g_file) return;
    std::fprintf(g_file, "[%8.3f] ", static_cast<double>(std::clock()) / CLOCKS_PER_SEC);
    std::vfprintf(g_file, fmt, ap);
    std::fputc('\n', g_file);
    std::fflush(g_file);
}
} // namespace

void Init(const char* path, bool enabled) {
    if (g_file) { std::fclose(g_file); g_file = nullptr; }
    if (enabled && path) g_file = std::fopen(path, "w");
}
void Write(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); VWrite(fmt, ap); va_end(ap);
}
void WriteLimited(int channel, int maxLines, const char* fmt, ...) {
    if (channel < 0 || channel > 7) channel = 7;
    if (g_counts[channel] >= maxLines) return;
    ++g_counts[channel];
    va_list ap; va_start(ap, fmt); VWrite(fmt, ap); va_end(ap);
    if (g_counts[channel] == maxLines) Write("(log channel %d reached its limit, further lines suppressed)", channel);
}
}}
