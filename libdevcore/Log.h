#pragma once

#include <chrono>
#include <ctime>

#include "Common.h"
#include "CommonData.h"
#include "FixedHash.h"
#include "Terminal.h"
#include "vector_ref.h"

/// The logging system's current verbosity.
#define LOG_JSON 1
#define LOG_PER_GPU 2
#if DEV_BUILD
#define LOG_CONNECT 32
#define LOG_SWITCH 64
#define LOG_SUBMIT 128
#define LOG_NEXT 256
#else
#define LOG_NEXT 4
#endif

extern unsigned g_logOptions;
extern bool g_logNoColor;
extern bool g_logSyslog;

namespace dev
{
/// A simple log-output function that prints log messages to stdout.
void simpleDebugOut(std::string const&);

/// Set the current thread's log name.
void setThreadName(char const* _n);

/// Set the current thread's log name.
std::string getThreadName();

/// The default logging channels. Each has an associated verbosity and three-letter prefix
/// (severity()
/// ). Channels should inherit from LogChannel and define severity() and verbosity.
struct LogChannel
{
    static int severity();
};
struct CritChannel : public LogChannel
{
    static int severity();
};
struct WarnChannel : public LogChannel
{
    static int severity();
};
struct NoteChannel : public LogChannel
{
    static int severity();
};

class LogOutputStreamBase
{
public:
    LogOutputStreamBase(int error);

    template <class T>
    void append(T const& _t)
    {
        m_sstr << _t;
    }

protected:
    std::stringstream m_sstr;  ///< The accrued log entry.
};

/// Logging class, iostream-like, that can be shifted to.
template <class I>
class LogOutputStream : LogOutputStreamBase
{
public:
    /// Construct a new object.
    /// If _term is true the the prefix info is terminated with a ']' character; if not it ends only
    /// with a '|' character.
    LogOutputStream() : LogOutputStreamBase(I::severity()) {}

    /// Destructor. Posts the accrued log entry to the g_logPost function.
    ~LogOutputStream() { simpleDebugOut(m_sstr.str()); }

    /// Shift arbitrary data to the log. Spaces will be added between items as required.
    template <class T>
    LogOutputStream& operator<<(T const& _t)
    {
        append(_t);
        return *this;
    }
};

#define clog(X) dev::LogOutputStream<X>()

// Simple cout-like stream objects for accessing common log channels.
// Dirties the global namespace, but oh so convenient...
#define cnote clog(dev::NoteChannel)
#define cwarn clog(dev::WarnChannel)
#define ccrit clog(dev::CritChannel)
}  // namespace dev
