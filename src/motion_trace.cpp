#include "motion_trace.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <string>

namespace atdd::motion_trace {
namespace {

constexpr std::size_t kCapacity = 16384;
std::array<Frame, kCapacity> g_frames{};
std::atomic<std::uint64_t> g_writeSequence{0};
std::atomic<std::uint64_t> g_readSequence{0};
std::atomic<std::uint64_t> g_dropped{0};
std::uint64_t g_written = 0;
HANDLE g_file = INVALID_HANDLE_VALUE;
std::int64_t g_qpcFrequency = 0;

bool WriteBytes(const char* data, const std::size_t size) noexcept {
    std::size_t offset = 0;
    while (offset < size) {
        const std::size_t remaining = size - offset;
        const DWORD requested = static_cast<DWORD>(
            remaining > static_cast<std::size_t>(MAXDWORD) ? MAXDWORD : remaining);
        DWORD written = 0;
        if (!WriteFile(g_file, data + offset, requested, &written, nullptr) || written == 0) {
            return false;
        }
        offset += written;
    }
    return true;
}

} // namespace

bool Initialize(const std::wstring& path, const std::int64_t qpcFrequency,
                std::wstring& error) {
    if (qpcFrequency <= 0) {
        error = L"Motion trace received an invalid QPC frequency.";
        return false;
    }
    g_file = CreateFileW(path.c_str(), GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_file == INVALID_HANDLE_VALUE) {
        error = L"Could not create motion trace: " + path + L" (Win32 error " +
                std::to_wstring(GetLastError()) + L")";
        return false;
    }
    g_qpcFrequency = qpcFrequency;
    constexpr char header[] =
        "qpc_seconds,tick_serial,older_serial,newer_serial,raw_alpha,"
        "phase_delay_ticks,alpha,flags,"
        "auth_x,auth_y,auth_z,auth_pitch,auth_yaw,auth_roll,"
        "base_x,base_y,base_z,base_pitch,base_yaw,base_roll,"
        "final_x,final_y,final_z,final_pitch,final_yaw,final_roll,"
        "poll_x,poll_y,deferred_x,deferred_y,pending_x,pending_y,history_pitch,history_yaw,"
        "pending_pitch,pending_yaw\r\n";
    if (!WriteBytes(header, sizeof(header) - 1U)) {
        error = L"Could not write the motion-trace header (Win32 error " +
                std::to_wstring(GetLastError()) + L")";
        CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
        return false;
    }
    FlushFileBuffers(g_file);
    return true;
}

void Record(const Frame& frame) noexcept {
    if (g_file == INVALID_HANDLE_VALUE) {
        return;
    }
    const std::uint64_t write = g_writeSequence.load(std::memory_order_relaxed);
    const std::uint64_t read = g_readSequence.load(std::memory_order_acquire);
    if (write - read >= kCapacity) {
        g_dropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_frames[write % kCapacity] = frame;
    g_writeSequence.store(write + 1U, std::memory_order_release);
}

FlushResult Flush() noexcept {
    FlushResult result{g_written, g_dropped.load(std::memory_order_relaxed)};
    if (g_file == INVALID_HANDLE_VALUE) {
        return result;
    }
    const std::uint64_t begin = g_readSequence.load(std::memory_order_relaxed);
    const std::uint64_t end = g_writeSequence.load(std::memory_order_acquire);
    if (begin == end) {
        return result;
    }

    std::string output;
    output.reserve(static_cast<std::size_t>(end - begin) * 384U);
    std::array<char, 1024> line{};
    for (std::uint64_t sequence = begin; sequence < end; ++sequence) {
        const Frame& frame = g_frames[sequence % kCapacity];
        const double seconds = static_cast<double>(frame.qpc) /
                               static_cast<double>(g_qpcFrequency);
        const int length = _snprintf_s(
            line.data(), line.size(), _TRUNCATE,
            "%.9f,%llu,%llu,%llu,%.9f,%.9f,%.9f,%u,"
            "%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,"
            "%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,"
            "%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,"
            "%d,%d,%lld,%lld,%lld,%lld,%.9g,%.9g,%.9g,%.9g\r\n",
            seconds,
            static_cast<unsigned long long>(frame.tickSerial),
            static_cast<unsigned long long>(frame.olderSerial),
            static_cast<unsigned long long>(frame.newerSerial), frame.rawAlpha,
            frame.phaseDelayTicks, frame.alpha, frame.flags,
            frame.authoritative.position.x, frame.authoritative.position.y,
            frame.authoritative.position.z, frame.authoritative.pitch,
            frame.authoritative.yaw, frame.authoritative.roll,
            frame.base.position.x, frame.base.position.y, frame.base.position.z,
            frame.base.pitch, frame.base.yaw, frame.base.roll,
            frame.final.position.x, frame.final.position.y, frame.final.position.z,
            frame.final.pitch, frame.final.yaw, frame.final.roll,
            frame.pollX, frame.pollY,
            static_cast<long long>(frame.deferredX),
            static_cast<long long>(frame.deferredY),
            static_cast<long long>(frame.pendingX),
            static_cast<long long>(frame.pendingY),
            frame.historyPitch, frame.historyYaw,
            frame.pendingPitch, frame.pendingYaw);
        if (length > 0) {
            output.append(line.data(), static_cast<std::size_t>(length));
        }
    }
    if (!output.empty() && WriteBytes(output.data(), output.size())) {
        g_readSequence.store(end, std::memory_order_release);
        g_written += end - begin;
        FlushFileBuffers(g_file);
    }
    result.written = g_written;
    result.dropped = g_dropped.load(std::memory_order_relaxed);
    return result;
}

} // namespace atdd::motion_trace
