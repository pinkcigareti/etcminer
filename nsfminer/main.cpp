
/* Copyright (C) 1883 Thomas Edison - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPLv3 license, which unfortunately won't be
 * written for another century.
 *
 * You should have received a copy of the LICENSE file with
 * this file.
 */

#if defined(__linux__)
#include <execinfo.h>
#endif

#include <algorithm>
#include <condition_variable>
#include <string>

#include <boost/algorithm/string.hpp>
#include <boost/function.hpp>
#include <boost/program_options.hpp>

#include <openssl/crypto.h>

#include <nsfminer/buildinfo.h>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

#include <libeth/Farm.h>
#if ETH_ETHASHCL
#include <libcl/CLMiner.h>
#endif
#if ETH_ETHASHCUDA
#include <libcuda/CUDAMiner.h>
#endif
#if ETH_ETHASHCPU
#include <libcpu/CPUMiner.h>
#endif
#include <libpool/PoolManager.h>

#if API_CORE
#include <libapi/ApiServer.h>
#include <regex>
#endif

#include <ethash/version.h>

using namespace std;
using namespace dev;
using namespace dev::eth;

using namespace boost::program_options;

// Global vars
bool g_running = false;
bool g_seqDAG = false;
bool g_exitOnError = false; // Whether or not miner should exit on mining threads errors
mutex g_seqDAGMutex;

condition_variable g_shouldstop;
boost::asio::io_service g_io_service; // The IO service itself

static bool should_list;

static void headers(vector<string>& h, bool color) {
    const string yellow(color ? EthYellow : "");
    const string white(color ? EthWhite : "");
    const string reset(color ? EthReset : "");

    auto* bi = nsfminer_get_buildinfo();
    stringstream ss;
    ss << yellow << "nsfminer " << bi->project_version << " (No stinkin' fees edition)";
    h.push_back(ss.str());

    ss.str("");
    ss << white << "Copyright 2021 Jean M. Cyr, Licensed under the terms";
    h.push_back(ss.str());

    ss.str("");
    ss << white << " of the GNU General Public License Version 3";
    h.push_back(ss.str());

    ss.str("");
    ss << white << "https://github.com/no-fee-ethereum-mining/nsfminer";
    h.push_back(ss.str());

    ss.str("");
    ss << white << "Build: " << bi->system_name << '/' << bi->build_type << '/' << bi->compiler_id;
    h.push_back(ss.str());

    ss.str("");
    ss << white << "3rd Party: ";
#if defined(__GNUC__)
    ss << "GCC " << bi->compiler_version << ", ";
#else
    ss << "MSVC " << bi->compiler_version << ", ";
#endif
#if ETH_ETHASHCUDA
    int v;
    if (cudaRuntimeGetVersion(&v) == cudaSuccess)
        ss << "CUDA " << v / 1000 << '.' << (v % 100) / 10 << ", ";

#endif
    ss << "Boost " << BOOST_VERSION / 100000 << '.' << BOOST_VERSION / 100 % 1000 << '.' << BOOST_VERSION % 100;
    h.push_back(ss.str());
    ss.str("");
    vector<string> sv;
    string s(SSLeay_version(SSLEAY_VERSION));
    boost::split(sv, s, boost::is_any_of(" "), boost::token_compress_on);
    ss << white << "3rd Party: OpenSSL " << sv[1] << ", Ethash " << ethash::version;
    h.push_back(ss.str());
    char username[64];
#if defined(__linux__)
    if (getlogin_r(username, sizeof(username)))
        strcpy(username, "unknown");
#else
    DWORD size = sizeof(username) - 1;
    if (!GetUserName(username, &size))
        strcpy(username, "unknown");
#endif
    ss.str("");
    ss << (color ? EthWhite : "") << "Running as user: " << username;
    h.push_back(ss.str());
}

static void on_help_module(string m) {
    static const vector<string> modules({
#if ETH_ETHASHCL
        "cl",
#endif
#if ETH_ETHASHCUDA
            "cu",
#endif
#if ETH_ETHASHCPU
            "cp",
#endif
#if API_CORE
            "api",
#endif
#ifdef _WIN32
            "env",
#endif
            "con", "test", "misc", "test", "conf", "reboot"
    });
    if (find(modules.begin(), modules.end(), m) != modules.end())
        return;

    string msg("The --help-module parameter must be one of the following:\n    ");
    bool first = true;
    for (auto& m : modules) {
        msg += first ? "" : ", " + m;
        first = false;
    }
    throw boost::program_options::error(msg);
}

static void on_nonce(string n) {
    for (const auto& c : n)
        if ((c < '0' || c > '9') && (c < 'a' || c > 'f') && (c < 'A' || c > 'F'))
            throw boost::program_options::error("The --nonce value must be a hex string");
}

static void on_verbosity(unsigned u) {
    if (u < LOG_NEXT)
        return;
    throw boost::program_options::error("The --verbosity value must be less than " + to_string(LOG_NEXT));
}

static void on_hwmon(unsigned u) {
    if (u < 3)
        return;
    throw boost::program_options::error("The --HWMON value must be 0, 1 or 2");
}

#if API_CORE
static void on_api_port(int i) {
    if (i >= -65535 && i <= 65535)
        return;
    throw boost::program_options::error("The --api-port value is out of range");
}
#endif

#if ETH_ETHASHCUDA
static void on_cu_block_size(unsigned b) {
    if (b == 32 || b == 64 || b == 128 || b == 256)
        return;
    throw boost::program_options::error("The --cu-block value is out of range");
}

static void on_cu_streams(unsigned s) {
    if (s == 1 || s == 2 || s == 4)
        return;
    throw boost::program_options::error("The --cu-streams value is out of range");
}
#endif

#if ETH_ETHASHCL
static void on_cl_local_work(unsigned b) {
    if (b == 64 || b == 128 || b == 256)
        return;
    throw boost::program_options::error("The --cl-work value is out of range");
}
#endif

class MinerCLI {
  public:
    enum class OperationMode { None, Simulation, Mining };

    MinerCLI() : m_cliDisplayTimer(g_io_service), m_io_strand(g_io_service) {
        // Initialize display timer as sleeper
        m_cliDisplayTimer.expires_from_now(boost::posix_time::pos_infin);
        m_cliDisplayTimer.async_wait(m_io_strand.wrap(
            boost::bind(&MinerCLI::cliDisplayInterval_elapsed, this, boost::asio::placeholders::error)));

        // Start io_service in it's own thread
        m_io_thread = thread{boost::bind(&boost::asio::io_service::run, &g_io_service)};

        // Io service is now live and running
        // All components using io_service should post to reference of g_io_service
        // and should not start/stop or even join threads (which heavily time consuming)
    }

    virtual ~MinerCLI() {
        m_cliDisplayTimer.cancel();
        g_io_service.stop();
        m_io_thread.join();
    }

    void cliDisplayInterval_elapsed(const boost::system::error_code& ec) {
        if (!ec && g_running) {
            if (g_logOptions & LOG_MULTI) {
                list<string> vs;
                Farm::f().Telemetry().strvec(vs);
                string s(vs.front());
                vs.pop_front();
                while (!vs.empty()) {
                    cnote << s << vs.front();
                    vs.pop_front();
                }
            } else
                cnote << Farm::f().Telemetry().str();
            // Restart timer
            m_cliDisplayTimer.expires_from_now(boost::posix_time::seconds(m_cliDisplayInterval));
            m_cliDisplayTimer.async_wait(m_io_strand.wrap(
                boost::bind(&MinerCLI::cliDisplayInterval_elapsed, this, boost::asio::placeholders::error)));
        }
    }

    static void signalHandler(int sig) {
        dev::setThreadName("main");

        switch (sig) {
#if defined(__linux__)
#define BACKTRACE_MAX_FRAMES 30
        case SIGSEGV:
            static bool in_handler = false;
            if (!in_handler) {
                int j, nptrs;
                void* buffer[BACKTRACE_MAX_FRAMES];
                char** symbols;

                in_handler = true;

                dev::setThreadName("main");
                cerr << "SIGSEGV encountered ...\n";
                cerr << "stack trace:\n";

                nptrs = backtrace(buffer, BACKTRACE_MAX_FRAMES);
                cerr << "backtrace() returned " << nptrs << " addresses\n";

                symbols = backtrace_symbols(buffer, nptrs);
                if (symbols == NULL) {
                    perror("backtrace_symbols()");
                    exit(EXIT_FAILURE); // Also exit 128 ??
                }
                for (j = 0; j < nptrs; j++)
                    cerr << symbols[j] << "\n";
                free(symbols);

                in_handler = false;
            }
            exit(128);
#undef BACKTRACE_MAX_FRAMES
#endif
        case (999U):
            // Compiler complains about the lack of
            // a case statement in Windows
            // this makes it happy.
            break;
        default:
            cerr << endl;
            ccrit << "Got interrupt ...";
            g_running = false;
            g_shouldstop.notify_all();
            break;
        }
    }

#if API_CORE
    static void ParseBind(const string& inaddr, string& outaddr, int& outport, bool advertise_negative_port) {
        regex pattern("([\\da-fA-F\\.\\:]*)\\:([\\d\\-]*)");
        smatch matches;

        if (regex_match(inaddr, matches, pattern)) {
            // Validate Ip address
            boost::system::error_code ec;
            outaddr = boost::asio::ip::address::from_string(matches[1], ec).to_string();
            if (ec)
                throw invalid_argument("Invalid Ip Address");

            // Parse port ( Let exception throw )
            outport = stoi(matches[2]);
            if (advertise_negative_port) {
                if (outport < -65535 || outport > 65535 || outport == 0)
                    throw invalid_argument("Invalid port number. Allowed non zero values in range [-65535 .. 65535]");
            } else {
                if (outport < 1 || outport > 65535)
                    throw invalid_argument("Invalid port number. Allowed non zero values in range [1 .. 65535]");
            }
        } else {
            throw invalid_argument("Invalid syntax");
        }
    }
#endif

    bool validateArgs(int argc, char** argv) {
        queue<string> warnings;
        bool cl_miner, cuda_miner, cpu_miner;
        vector<string> pools;

        options_description general("General options");
        options_description test("Test options");
        options_description misc("Miscellaneous options");
#if ETH_ETHASHCL
        options_description cl("OpenCL options");
#endif
#if ETH_ETHASHCUDA
        options_description cu("CUDA options");
#endif
#if ETH_ETHASHCPU
        options_description cp("CPU options");
#endif
#if API_CORE
        options_description api("API options");
#endif

        // clang-format off

        general.add_options()

            ("help,h", "This help message")("help-module,H",
                value<string>()->notifier(on_help_module),

                "Help for a given module, one of: "
#if ETH_ETHASHCL
                "cl, "
#endif
#if ETH_ETHASHCUDA
                "cu, "
#endif
#if ETH_ETHASHCPU
                "cp, "
#endif
#if API_CORE
                "api, "
#endif
                "misc, "
#ifdef _WIN32
                "env, "
#endif
                "con, test, conf or reboot")

            ("version,V",

                "The version number")

            ("pool,P", value<vector<string>>()->multitoken(),

                "One or more Stratum pool or http (getWork) connection as URL(s)\n\n"
                "scheme://[user[.workername][:password]@]hostname:port[/...]\n\n"
                "For details and some samples how to fill in this value please use\n"
                "nsfminer --help-module con\n\n")

            ("config,F", value<string>(),

                "Configuration file name. See '-H conf' for details.")

#if ETH_ETHASHCL
            ("opencl,G",

                "Mine/Benchmark using OpenCL only")
#endif
#if ETH_ETHASHCUDA
            ("cuda,U",

                "Mine/Benchmark using CUDA only")
#endif
#if ETH_ETHASHCPU
            ("cpu",

                "Development ONLY ! (NO MINING)")
#endif
            ;

        misc.add_options()

            ("verbosity,v",

                value<unsigned>()->default_value(0)->notifier(on_verbosity),

                "Set output verbosity level. Use the sum of :\n"
                "1 - log per GPU status lines\n"
                "2 - log per GPU solutions\n"
#ifdef DEV_BUILD
                "\n16 - log stratum messages\n"
                "32 - log connection events\n"
                "64 - log job switch times\n"
                "128 - log share submit times"
#endif
            )

            ("getwork-recheck", value<unsigned>()->default_value(500),

                "Set polling interval for new work in getWork mode. "
                "Value expressed in milliseconds. "
                "It has no meaning in stratum mode")

            ("retry-delay", value<unsigned>()->default_value(0),

                "Delay in seconds before reconnection retry")

            ("retry-max", value<unsigned>()->default_value(3),

                "Set number of reconnection retries to same pool. "
                "Set to 0 for infinite retries.")

            ("work-timeout", value<unsigned>()->default_value(180),

                "If no new work received from pool after this "
                "amount of time the connection is dropped. "
                "Value expressed in seconds.")

            ("response-timeout", value<unsigned>()->default_value(2),

                "If no response from pool to a stratum message "
                "after this amount of time the connection is dropped")

            ("report-hashrate,R",

                "Report miner hash rate to the pool")

            ("display-interval", value<unsigned>()->default_value(5),

                "Statistic display interval in seconds")

            ("HWMON", value<unsigned>()->default_value(0)->notifier(on_hwmon),

                "GPU hardware monitoring level. Can be one of:\n"
                "0 - No monitoring\n"
                "1 - Monitor temperature and fan percentage\n"
                "2 - As 1 plus monitor power drain")

            ("exit",

                "Stop miner whenever an error is encountered")

            ("failover-timeout", value<unsigned>()->default_value(0),

                "Sets the number of minutes miner can stay "
                "connected to a fail-over pool before trying to "
                "reconnect to the primary (the first) connection.")

            ("nocolor",

                "Monochrome display log lines")

            ("syslog",

                "Use syslog appropriate output (drop timestamp "
                "and channel prefix)")

#if ETH_ETHASHCL || ETH_ETHASHCUDA || ETH_ETHASH_CPU

            ("list-devices,L",

                "Lists the detected OpenCL/CUDA devices and "
                "exits. Can be combined with -G or -U flags")
#endif
            ("tstop", value<unsigned>()->default_value(0),

                "Suspend mining on GPU which temperature is above "
                "this threshold. Implies --HWMON 1. "
                "If not set or zero no temp control is performed")

            ("tstart", value<unsigned>()->default_value(0),

                "Resume mining on previously overheated GPU when "
                "temp drops below this threshold. Implies --HWMON 1. "
                "Must be lower than --tstart")

            ("nonce,n", value<string>()->default_value("")->notifier(on_nonce),

                "Hex string specifying the upper bits of miner's "
                "start nonce. Can be used to ensure multiple miners "
                "are not searching overlapping nonce ranges.")

            ("devices", value<vector<unsigned>>()->multitoken(),

                "List of space separated device numbers to be used")

            ("seq",

                "Generate DAG sequentially, one GPU at a time.");

#if API_CORE

        api.add_options()

            ("api-bind", value<string>()->default_value(""),

                "Set the API address:port the miner should listen "
                "on. Use negative port number for readonly mode")

            ("api-port", value<int>()->default_value(0)->notifier(on_api_port),

                "Set the API port, the miner should listen on all "
                "bound addresses. Use negative numbers for readonly "
                "mode")

            ("api-password", value<string>()->default_value(""),

                "Set the password to protect interaction with API "
                "server. If not set, any connection is granted access. "
                "Be advised passwords are sent unencrypted");
#endif
#if ETH_ETHASHCUDA
        cu.add_options()

            ("cu-block", value<unsigned>()->default_value(128)->notifier(on_cu_block_size),

                "Set the block size, valid values are 32, 64, 128, or 256")

            ("cu-streams", value<unsigned>()->default_value(2)->notifier(on_cu_streams),

                "Set the number of streams per GPU, valid values 1, 2 or 4");
#endif
#if ETH_ETHASHCL
        cl.add_options()

            ("cl-work", value<unsigned>()->default_value(128)->notifier(on_cl_local_work),

                "Set the work group size, valid values are 64 128 or 256")

            ("cl-split",

                "Force split-DAG mode. May improve performance on older GPU models.");
#endif
        test.add_options()

            ("benchmark,M", value<unsigned>(),

                "Mining test. Used to test hashing speed. "
                "Specify the block number to test on.")

            ("simulate,Z", value<unsigned>(),

                "Mining test. Used to test hashing speed. "
                "Specify the block number to test on.");

        // clang-format on

        options_description all("All options");
        all.add(general)
#if ETH_ETHASHCL
            .add(cl)
#endif
#if ETH_ETHASHCUDA
            .add(cu)
#endif
#if ETH_ETHASHCPU
            .add(cp)
#endif
#if API_CORE
            .add(api)
#endif
            .add(test)
            .add(misc);

        options_description visible("General options");
        visible.add(general);

        variables_map vm;
        try {

            parsed_options parsed = command_line_parser(argc, argv).options(all).allow_unregistered().run();
            store(parsed, vm);

            if (vm.count("config")) {
                ifstream ifs(vm["config"].as<string>().c_str());
                if (!ifs) {
                    string msg("Could not open file '");
                    msg += vm["config"].as<string>() + "'.";
                    throw boost::program_options::error(msg);
                }
                // Read the whole file into a string
                stringstream ss;
                ss << ifs.rdbuf();
                vector<string> args;
                boost::split(args, ss.str(), boost::is_any_of(" \n\r\t"), boost::token_compress_on);
                store(command_line_parser(args).options(all).run(), vm);
            }

            notify(vm);
            vector<string> unknown = collect_unrecognized(parsed.options, include_positional);
            if (unknown.size()) {
                cout << endl << "Error: Unknown parameter(s):";
                for (const auto& u : unknown)
                    cout << ' ' << u;
                cout << endl << endl;
                return false;
            }
        } catch (boost::program_options::error& e) {
            cout << endl << "Error: " << e.what() << endl << endl;
            return false;
        }

        if (vm.count("help")) {
            cout << endl << visible << endl;
            return false;
        }

        if (vm.count("version")) {
            vector<string> vs;
            headers(vs, false);
            cout << endl;
            for (auto& v : vs)
                cout << v << endl;
            cout << endl;
            return false;
        }

        if (vm.count("help-module")) {
            const string& s = vm["help-module"].as<string>();
            if (s == "con")
                cout << "\n\nConnections specifications :\n\n"
                     << "    Whether you need to connect to a stratum pool or to make use of\n"
                     << "    getWork polling mode (generally used to solo mine) you need to "
                        "specify\n"
                     << "    the connection  making use of -P command line argument filling up the\n"
                     << "    URL. The URL is in the form:\n\n "
                     << "    scheme://[user[.workername][:password]@]hostname:port[/...].\n\n"
                     << "    where 'scheme' can be any of :\n\n"
                     << "    getwork    for http getWork mode\n"
                     << "    stratum    for tcp stratum mode\n"
                     << "    stratums   for tcp encrypted stratum mode\n"
                     << "    Example 1: -P getwork://127.0.0.1:8545\n"
                     << "    Example 2: "
                        "-P "
                        "stratums://0x012345678901234567890234567890123.miner1@ethermine.org:5555\n"
                     << "    Example 3: "
                        "-P stratum://0x012345678901234567890234567890123.miner1@nanopool.org:9999/"
                        "john.doe%40gmail.com\n"
                     << "    Example 4: "
                        "-P stratum://0x012345678901234567890234567890123@nanopool.org:9999/miner1/"
                        "john.doe%40gmail.com\n\n"
                     << "    Please note: if your user or worker or password do contain characters\n"
                     << "    which may impair the correct parsing (namely any of . : @ # ?) you "
                        "have\n"
                     << "    to enclose those values in backticks( ` ASCII 096) or Url Encode them\n"
                     << "    Also note that backtick has a special meaning in *nix environments "
                        "thus\n"
                     << "    you need to further escape those backticks with backslash.\n\n"
                     << "    Example : -P stratums://\\`account.121\\`.miner1:x@ethermine.org:5555\n"
                     << "    Example : -P stratums://account%2e121.miner1:x@ethermine.org:5555\n"
                     << "    (In Windows backslashes are not needed)\n\n"
                     << "    Common url encoded chars are\n"
                     << "    . (dot)      %2e\n"
                     << "    : (column)   %3a\n"
                     << "    @ (at sign)  %40\n"
                     << "    ? (question) %3f\n"
                     << "    # (number)   %23\n"
                     << "    / (slash)    %2f\n"
                     << "    + (plus)     %2b\n\n"
                     << "    You can add as many -P arguments as you want. Every -P specification\n"
                     << "    after the first one behaves as fail-over connection. When also the\n"
                     << "    the fail-over disconnects miner passes to the next connection\n"
                     << "    available and so on till the list is exhausted. At that moment\n"
                     << "    miner restarts the connection cycle from the first one.\n"
                     << "    An exception to this behavior is ruled by the --failover-timeout\n"
                     << "    command line argument. See 'nsfminer -H misc' for details.\n\n"
                     << "    The special notation '-P exit' stops the failover loop.\n"
                     << "    When miner reaches this kind of connection it simply quits.\n\n"
                     << "    When using stratum mode miner tries to auto-detect the correct\n"
                     << "    flavour provided by the pool. Should be fine in 99% of the cases.\n"
                     << "    Nevertheless you might want to fine tune the stratum flavour by\n"
                     << "    any of of the following valid schemes :\n\n"
                     << "    " << URI::KnownSchemes(ProtocolFamily::STRATUM) << "\n\n"
                     << "    where a scheme is made up of two parts, the stratum variant + the tcp\n"
                     << "    transport protocol\n\n"
                     << "    Stratum variants :\n\n"
                     << "        stratum     Stratum\n"
                     << "        stratum1    Eth Proxy compatible\n"
                     << "        stratum2    EthereumStratum 1.0.0 (nicehash)\n"
                     << "        stratum3    EthereumStratum 2.0.0\n\n"
                     << "    Transport variants :\n\n"
                     << "        tcp         Unencrypted tcp connection\n"
                     << "        ssl         Encrypted tcp connection\n\n";
            else if (s == "test") // Simulation
                cout << endl << test << endl;
#if ETH_ETHASHCL
            else if (s == "cl") // opencl
                cout << endl << cl << endl;
#endif
#if ETH_ETHASHCUDA
            else if (s == "cu") // cuda
                cout << endl << cu << endl;
#endif
#if ETH_ETHASHCPU
            else if (s == "cp") // cpu
                cout << endl << cp << endl;
#endif
#if API_CORE
            else if (s == "api") // programming interface
                cout << endl << api << endl;
#endif
            else if (s == "misc") // miscellaneous
                cout << endl << misc << endl;
            else if (s == "conf") // configuration
                cout << "\nConfiguration file details:\n\n"
                     << "Place command line options in a file, for example:\n\n"
                     << "  --api-port 40000\n"
                     << "  --report-hashrate\n"
                     << "  --HWMON 1\n"
                     << "  -P\n"
                     << "    stratums://0x2ceCE0...b3caa0F6e86.rig0@eth-us-east.flexpool.io:5555\n"
                     << "    stratums://0x2ceCE0...b3caa0F6e86.rig0@eth-us-west.flexpool.io:5555\n"
                     << "  -v 7 --display-interval 15\n\n";
#ifdef _WIN32
            else if (s == "env")
                cout << "\nEnvironment variables :\n\n"
                     << "    If you need or do feel more comfortable you can set the following\n"
                     << "    environment variables. Please respect letter casing.\n\n"
                     << "    SSL_CERT_FILE  Set to the full path to of your CA certificates\n"
                     << "                   file if it is not in standard path :\n"
                     << "                   /etc/ssl/certs/ca-certificates.crt.\n\n";
#endif
            else if (s == "reboot")
                cout << "\nMiner reboots:\n\n"
                     << "    The user may create a reboot script that will be invoked\n"
                     << "    if ever the miner deems it needs to restart. That can happen\n"
                     << "    if requested via the API, or if the miner detects a hung\n"
                     << "    GPU. The script is invoked with 1 parameter, 'api_miner_reboot'\n"
                     << "    for API reboots, and 'hung_miner_reboot' for hung GPUs\n\n"
                     << "    The script needs a specific file name and must be first in\n"
                     << "    the search path.\n\n"
                     << "    For Linux:   reboot.sh\n\n"
                     << "    For Windows: reboot.bat\n\n";
            return false;
        }

        g_logOptions = vm["verbosity"].as<unsigned>();
        g_logNoColor = vm.count("nocolor");
        g_logSyslog = vm.count("syslog");
        g_exitOnError = vm.count("exit");
        g_seqDAG = vm.count("seq");

        m_PoolSettings.getWorkPollInterval = vm["getwork-recheck"].as<unsigned>();
        m_PoolSettings.connectionMaxRetries = vm["retry-max"].as<unsigned>();
        m_PoolSettings.delayBeforeRetry = vm["retry-delay"].as<unsigned>();
        m_PoolSettings.noWorkTimeout = vm["work-timeout"].as<unsigned>();
        m_PoolSettings.noResponseTimeout = vm["response-timeout"].as<unsigned>();
        m_PoolSettings.reportHashrate = vm.count("report-hashrate");
        m_PoolSettings.poolFailoverTimeout = vm["failover-timeout"].as<unsigned>();
        if (vm.count("simulate")) {
            m_bench = true;
            m_PoolSettings.benchmarkBlock = vm["simulate"].as<unsigned>();
        }
        if (vm.count("benchmark")) {
            m_bench = true;
            m_PoolSettings.benchmarkBlock = vm["benchmark"].as<unsigned>();
        }

        m_cliDisplayInterval = vm["display-interval"].as<unsigned>();
        should_list = m_shouldListDevices = vm.count("list-devices");

        if (vm.count("devices"))
            for (auto& d : vm["devices"].as<vector<unsigned>>())
                m_devices.push_back(d);

        m_FarmSettings.hwMon = vm["HWMON"].as<unsigned>();
        m_FarmSettings.nonce = vm["nonce"].as<string>();

#if ETH_ETHASHCUDA
        m_FarmSettings.cuBlockSize = vm["cu-block"].as<unsigned>();
        m_FarmSettings.cuStreams = vm["cu-streams"].as<unsigned>();
#endif

#if ETH_ETHASHCL
        m_FarmSettings.clGroupSize = vm["cl-work"].as<unsigned>();
        m_FarmSettings.clSplit = vm.count("cl-split");
#endif

        m_FarmSettings.tempStop = vm["tstop"].as<unsigned>();
        m_FarmSettings.tempStart = vm["tstart"].as<unsigned>();

        cl_miner = vm.count("opencl");
        cuda_miner = vm.count("cuda");
        cpu_miner = vm.count("cpu");
        if (vm.count("pool"))
            for (auto& p : vm["pool"].as<vector<string>>())
                pools.push_back(p);

#if API_CORE
        m_api_bind = vm["api-bind"].as<string>();
        m_api_port = vm["api-port"].as<int>();
        m_api_password = vm["api-password"].as<string>();
        if (m_api_bind != "") {
            try {
                ParseBind(m_api_bind, m_api_address, m_api_port, true);
            } catch (const exception&) {
                cout << "Error: --api-bind address invalid\n\n";
                return false;
            }
        }
#endif

        if (cl_miner)
            m_minerType = MinerType::CL;
        else if (cuda_miner)
            m_minerType = MinerType::CUDA;
        else if (cpu_miner)
            m_minerType = MinerType::CPU;
        else if (cl_miner && cuda_miner)
            m_minerType = MinerType::Mixed;
        else
            m_minerType = MinerType::Mixed;

        //  Operation mode Simulation do not require pool definitions
        //  Operation mode Stratum or GetWork do need at least one

        if (m_bench) {
            m_mode = OperationMode::Simulation;
            pools.clear();
            m_PoolSettings.connections.push_back(shared_ptr<URI>(new URI("simulation://localhost:0", true)));
        } else
            m_mode = OperationMode::Mining;

        if (!m_shouldListDevices && (m_mode != OperationMode::Simulation)) {
            if (!pools.size())
                throw invalid_argument("At least one pool definition required. See -P argument.");

            for (size_t i = 0; i < pools.size(); i++) {
                string url = pools.at(i);
                if (url == "exit") {
                    if (i == 0)
                        throw invalid_argument("'exit' failover directive can't be the first in -P arguments list.");
                    else
                        url = "stratum+tcp://-:x@exit:0";
                }

                try {
                    shared_ptr<URI> uri = shared_ptr<URI>(new URI(url));
                    m_PoolSettings.connections.push_back(uri);
                } catch (const exception& _ex) {
                    string what = _ex.what();
                    throw runtime_error("Bad pool URI : " + what);
                }
            }
        }

        if (m_FarmSettings.tempStop) {
            // If temp threshold set HWMON at least to 1
            m_FarmSettings.hwMon = max((unsigned int)m_FarmSettings.hwMon, 1U);
            if (m_FarmSettings.tempStop <= m_FarmSettings.tempStart) {
                string what = "-tstop must be greater than -tstart";
                throw invalid_argument(what);
            }
        }

        // Output warnings if any
        while (warnings.size()) {
            cout << warnings.front() << endl;
            warnings.pop();
        }

        return true;
    }

    void execute() {
#if ETH_ETHASHCL
        if (m_minerType == MinerType::CL || m_minerType == MinerType::Mixed)
            CLMiner::enumDevices(m_DevicesCollection);
#endif
#if ETH_ETHASHCUDA
        if (m_minerType == MinerType::CUDA || m_minerType == MinerType::Mixed)
            CUDAMiner::enumDevices(m_DevicesCollection);
#endif
#if ETH_ETHASHCPU
        if (m_minerType == MinerType::CPU)
            CPUMiner::enumDevices(m_DevicesCollection);
#endif

        // Can't proceed without any GPU
        if (!m_DevicesCollection.size())
            throw runtime_error("No usable mining devices found");

        // If requested list detected devices and exit
        if (should_list) {
            cout << setw(4) << " Id ";
            cout << setiosflags(ios::left) << setw(13) << "Pci Id    ";
            cout << setw(5) << "Type ";
            cout << setw(30) << "Name                          ";

#if ETH_ETHASHCUDA
            if (m_minerType == MinerType::CUDA || m_minerType == MinerType::Mixed) {
                cout << setw(5) << "CUDA ";
                cout << setw(4) << "SM  ";
            }
#endif
#if ETH_ETHASHCL
            if (m_minerType == MinerType::CL || m_minerType == MinerType::Mixed)
                cout << setw(5) << "CL   ";
#endif
            cout << resetiosflags(ios::left) << setw(13) << "Total Memory"
                 << " ";

            cout << resetiosflags(ios::left) << endl;
            cout << setw(4) << "--- ";
            cout << setiosflags(ios::left) << setw(13) << "------------";
            cout << setw(5) << "---- ";
            cout << setw(30) << "----------------------------- ";

#if ETH_ETHASHCUDA
            if (m_minerType == MinerType::CUDA || m_minerType == MinerType::Mixed) {
                cout << setw(5) << "---- ";
                cout << setw(4) << "--- ";
            }
#endif
#if ETH_ETHASHCL
            if (m_minerType == MinerType::CL || m_minerType == MinerType::Mixed)
                cout << setw(5) << "---- ";
#endif
            cout << resetiosflags(ios::left) << setw(13) << "------------"
                 << " ";

            cout << resetiosflags(ios::left) << endl;
            minerMap::iterator it = m_DevicesCollection.begin();
            while (it != m_DevicesCollection.end()) {
                auto i = distance(m_DevicesCollection.begin(), it);
                cout << setw(3) << i << " ";
                cout << setiosflags(ios::left) << setw(13) << it->first;
                cout << setw(5);
                switch (it->second.type) {
                case DeviceTypeEnum::Cpu:
                    cout << "Cpu";
                    break;
                case DeviceTypeEnum::Gpu:
                    cout << "Gpu";
                    break;
                case DeviceTypeEnum::Accelerator:
                    cout << "Acc";
                    break;
                default:
                    break;
                }
                cout << setw(30) << (it->second.boardName).substr(0, 28);
#if ETH_ETHASHCUDA
                if (m_minerType == MinerType::CUDA || m_minerType == MinerType::Mixed) {
                    cout << setw(5) << (it->second.cuDetected ? "Yes" : "");
                    cout << setw(4) << it->second.cuCompute;
                }
#endif
#if ETH_ETHASHCL
                if (m_minerType == MinerType::CL || m_minerType == MinerType::Mixed)
                    cout << setw(5) << (it->second.clDetected ? "Yes" : "");
#endif
                cout << resetiosflags(ios::left) << setw(13) << getFormattedMemory((double)it->second.totalMemory)
                     << " ";
                cout << resetiosflags(ios::left) << endl;
                it++;
            }

            return;
        }

        // Subscribe devices with appropriate Miner Type
        // Use CUDA first when available then, as second, OpenCL

#if ETH_ETHASHCUDA
        if (m_minerType == MinerType::CUDA || m_minerType == MinerType::Mixed)
            for (auto it = m_DevicesCollection.begin(); it != m_DevicesCollection.end(); it++) {
                if (!it->second.cuDetected || it->second.subscriptionType != DeviceSubscriptionTypeEnum::None)
                    continue;
                unsigned d = (unsigned)distance(m_DevicesCollection.begin(), it);
                if (m_devices.empty() || find(m_devices.begin(), m_devices.end(), d) != m_devices.end())
                    it->second.subscriptionType = DeviceSubscriptionTypeEnum::Cuda;
            }
#endif
#if ETH_ETHASHCL
        if (m_minerType == MinerType::CL || m_minerType == MinerType::Mixed)
            for (auto it = m_DevicesCollection.begin(); it != m_DevicesCollection.end(); it++) {
                if (!it->second.clDetected || it->second.subscriptionType != DeviceSubscriptionTypeEnum::None)
                    continue;
                unsigned d = (unsigned)distance(m_DevicesCollection.begin(), it);
                if (m_devices.empty() || find(m_devices.begin(), m_devices.end(), d) != m_devices.end())
                    it->second.subscriptionType = DeviceSubscriptionTypeEnum::OpenCL;
            }
#endif
#if ETH_ETHASHCPU
        if (m_minerType == MinerType::CPU)
            for (auto it = m_DevicesCollection.begin(); it != m_DevicesCollection.end(); it++)
                it->second.subscriptionType = DeviceSubscriptionTypeEnum::Cpu;
#endif
        // Count of subscribed devices
        int subscribedDevices = 0;
        for (auto it = m_DevicesCollection.begin(); it != m_DevicesCollection.end(); it++)
            if (it->second.subscriptionType != DeviceSubscriptionTypeEnum::None)
                subscribedDevices++;

        // If no OpenCL and/or CUDA devices subscribed then throw error
        if (!subscribedDevices)
            throw runtime_error("No mining device selected. Aborting ...");

        // Enable
        g_running = true;

        // Signal traps
#if defined(__linux__)
        signal(SIGSEGV, MinerCLI::signalHandler);
#endif
        signal(SIGINT, MinerCLI::signalHandler);
        signal(SIGTERM, MinerCLI::signalHandler);

        // Initialize Farm
        new Farm(m_DevicesCollection, m_FarmSettings);

        // Run Miner
        doMiner();
    }

  private:
    void doMiner() {
        new PoolManager(m_PoolSettings);
        if (m_mode != OperationMode::Simulation)
            for (auto conn : m_PoolSettings.connections)
                cnote << "Configured pool " << conn->Host() + ":" + to_string(conn->Port());

#if API_CORE

        ApiServer api(m_api_address, m_api_port, m_api_password);
        if (m_api_port)
            api.start();
#endif

        // Start PoolManager
        PoolManager::p().start();

        // Initialize display timer as sleeper with proper interval
        m_cliDisplayTimer.expires_from_now(boost::posix_time::seconds(m_cliDisplayInterval));
        m_cliDisplayTimer.async_wait(m_io_strand.wrap(
            boost::bind(&MinerCLI::cliDisplayInterval_elapsed, this, boost::asio::placeholders::error)));

        // Stay in non-busy wait till signals arrive
        unique_lock<mutex> clilock(m_climtx);
        while (g_running)
            g_shouldstop.wait(clilock);

#if API_CORE

        // Stop Api server
        if (api.isRunning())
            api.stop();

#endif
        if (PoolManager::p().isRunning())
            PoolManager::p().stop();

        cnote << "Terminated!";
        return;
    }

    // Global boost's io_service
    thread m_io_thread;                            // The IO service thread
    boost::asio::deadline_timer m_cliDisplayTimer; // The timer which ticks display lines
    boost::asio::io_service::strand m_io_strand;   // A strand to serialize posts in
                                                   // multithreaded environment
    // Physical Mining Devices descriptor
    minerMap m_DevicesCollection;

    // Mining options
    MinerType m_minerType = MinerType::Mixed;
    OperationMode m_mode = OperationMode::None;
    bool m_shouldListDevices = false;

    FarmSettings m_FarmSettings; // Operating settings for Farm
    PoolSettings m_PoolSettings; // Operating settings for PoolManager

    // -- CLI Interface related params
    unsigned m_cliDisplayInterval = 5; // Display stats/info interval in seconds

    // -- CLI Flow control
    mutex m_climtx;

    vector<unsigned> m_devices;

    bool m_bench = false;

#if API_CORE
    // -- API and Http interfaces related params
    string m_api_bind;                // API interface binding address in form <address>:<port>
    string m_api_address = "0.0.0.0"; // API interface binding address (Default any)
    int m_api_port = 0;               // API interface binding port
    string m_api_password;            // API interface write protection password
#endif

};

int main(int argc, char** argv) {
    // Return values
    // 0 - Normal exit
    // 1 - Invalid/Insufficient command line arguments
    // 2 - Runtime error
    // 3 - Other exceptions
    // 4 - Unknown exception

    dev::setThreadName("miner");

#if defined(_WIN32)
    // Need to change the code page from the default OEM code page (437) so that
    // UTF-8 characters are displayed correctly in the console
    SetConsoleOutputCP(CP_UTF8);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode;
        if (GetConsoleMode(hOut, &dwMode))
            SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }

    // prevent system sleep
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED);
#endif

    if (argc < 2) {
        cout << "No arguments specified.";
        cout << "Try 'nsfminer --help' to get a list of arguments.";
        return 1;
    }

    try {
        MinerCLI cli;

        try {
            // Set env vars controlling GPU driver behavior.
            setenv("GPU_MAX_HEAP_SIZE", "100");
            setenv("GPU_MAX_ALLOC_PERCENT", "100");
            setenv("GPU_SINGLE_ALLOC_PERCENT", "100");
            setenv("GPU_USE_SYNC_OBJECTS", "1");

            // Argument validation either throws exception
            // or returns false which means do not continue
            if (!cli.validateArgs(argc, argv))
                return 0;

            if (g_logSyslog)
                g_logNoColor = true;

            if (!should_list) {
                vector<string> vs;
                headers(vs, !g_logNoColor);
                for (auto& v : vs)
                    cnote << v;
            }

            cli.execute();

            cout << endl << endl;
            return 0;
        } catch (boost::program_options::error& e) {
            cout << "\nError: " << e.what() << "\n\n";
            return 1;
        } catch (runtime_error& e) {
            cout << "\nError: " << e.what() << "\n\n";
            return 2;
        } catch (exception& e) {
            cout << "\nError: " << e.what() << "\n\n";
            return 3;
        } catch (...) {
            cout << "\n\nError: Unknown failure occurred.\n\n";
            return 4;
        }
    } catch (const exception& e) {
        cout << "Could not initialize CLI interface\nError: " << e.what() << "\n\n";
        return 4;
    }
}
