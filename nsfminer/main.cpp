
#include <CLI/CLI.hpp>

#include <nsfminer/buildinfo.h>
#include <condition_variable>

#include <openssl/crypto.h>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

#include <libethcore/Farm.h>
#if ETH_ETHASHCL
#include <libethash-cl/CLMiner.h>
#endif
#if ETH_ETHASHCUDA
#include <libethash-cuda/CUDAMiner.h>
#endif
#if ETH_ETHASHCPU
#include <libethash-cpu/CPUMiner.h>
#endif
#include <libpoolprotocols/PoolManager.h>

#if API_CORE
#include <libapicore/ApiServer.h>
#include <regex>
#endif

#include <ethash/version.h>

#if defined(__linux__)
#include <execinfo.h>
#elif defined(_WIN32)
#include <Windows.h>
#endif

using namespace std;
using namespace dev;
using namespace dev::eth;

// Global vars
bool g_running = false;
bool g_exitOnError = false;  // Whether or not miner should exit on mining threads errors

condition_variable g_shouldstop;
boost::asio::io_service g_io_service;  // The IO service itself

struct MiningChannel : public LogChannel
{
    static bool name() { return false; }
    static const int verbosity = 2;
};

#define minelog clog(MiningChannel)

#if ETH_DBUS
#include <nsfminer/DBusInt.h>
#endif

static void headers(vector<string>& h, bool color)
{
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
    ss << "Boost " << BOOST_VERSION / 100000 << '.' << BOOST_VERSION / 100 % 1000 << '.'
       << BOOST_VERSION % 100;
    vector<string> sv;
    string s(SSLeay_version(SSLEAY_VERSION));
    boost::split(sv, s, boost::is_any_of(" "), boost::token_compress_on);
    ss << ", OpenSSL " << sv[1];
    h.push_back(ss.str());

    ss.str("");
    ss << white << "3rd Party: CLI11 " CLI11_VERSION ", Ethash " << ethash::version;
    h.push_back(ss.str());
}

class MinerCLI
{
public:
    enum class OperationMode
    {
        None,
        Simulation,
        Mining
    };

    MinerCLI() : m_cliDisplayTimer(g_io_service), m_io_strand(g_io_service)
    {
        // Initialize display timer as sleeper
        m_cliDisplayTimer.expires_from_now(boost::posix_time::pos_infin);
        m_cliDisplayTimer.async_wait(m_io_strand.wrap(boost::bind(
            &MinerCLI::cliDisplayInterval_elapsed, this, boost::asio::placeholders::error)));

        // Start io_service in it's own thread
        m_io_thread = thread{boost::bind(&boost::asio::io_service::run, &g_io_service)};

        // Io service is now live and running
        // All components using io_service should post to reference of g_io_service
        // and should not start/stop or even join threads (which heavily time consuming)
    }

    virtual ~MinerCLI()
    {
        m_cliDisplayTimer.cancel();
        g_io_service.stop();
        m_io_thread.join();
    }

    void cliDisplayInterval_elapsed(const boost::system::error_code& ec)
    {
        if (!ec && g_running)
        {
            string logLine =
                PoolManager::p().isConnected() ? Farm::f().Telemetry().str() : "Not connected";
            minelog << logLine;

#if ETH_DBUS
            dbusint.send(Farm::f().Telemetry().str().c_str());
#endif
            // Resubmit timer
            m_cliDisplayTimer.expires_from_now(boost::posix_time::seconds(m_cliDisplayInterval));
            m_cliDisplayTimer.async_wait(m_io_strand.wrap(boost::bind(
                &MinerCLI::cliDisplayInterval_elapsed, this, boost::asio::placeholders::error)));
        }
    }

    static void signalHandler(int sig)
    {
        dev::setThreadName("main");

        switch (sig)
        {
#if defined(__linux__)
#define BACKTRACE_MAX_FRAMES 100
        case SIGSEGV:
            static bool in_handler = false;
            if (!in_handler)
            {
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
                if (symbols == NULL)
                {
                    perror("backtrace_symbols()");
                    exit(EXIT_FAILURE);  // Also exit 128 ??
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
            cwarn << "Got interrupt ...";
            g_running = false;
            g_shouldstop.notify_all();
            break;
        }
    }

#if API_CORE

    static void ParseBind(
        const string& inaddr, string& outaddr, int& outport, bool advertise_negative_port)
    {
        regex pattern("([\\da-fA-F\\.\\:]*)\\:([\\d\\-]*)");
        smatch matches;

        if (regex_match(inaddr, matches, pattern))
        {
            // Validate Ip address
            boost::system::error_code ec;
            outaddr = boost::asio::ip::address::from_string(matches[1], ec).to_string();
            if (ec)
                throw invalid_argument("Invalid Ip Address");

            // Parse port ( Let exception throw )
            outport = stoi(matches[2]);
            if (advertise_negative_port)
            {
                if (outport < -65535 || outport > 65535 || outport == 0)
                    throw invalid_argument(
                        "Invalid port number. Allowed non zero values in range [-65535 .. 65535]");
            }
            else
            {
                if (outport < 1 || outport > 65535)
                    throw invalid_argument(
                        "Invalid port number. Allowed non zero values in range [1 .. 65535]");
            }
        }
        else
        {
            throw invalid_argument("Invalid syntax");
        }
    }
#endif

    bool validateArgs(int argc, char** argv)
    {
        queue<string> warnings;

        CLI::App app("\nnsfminer - GPU Ethash miner");

        bool bhelp = false;
        string shelpExt;

        app.set_help_flag();
        app.add_flag("-h,--help", bhelp, "Show help");

        app.add_set(
            "-H,--help-ext", shelpExt,
            {
                "con", "test",
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
                    "misc", "env"
            },
            "", true);

        bool version = false;

        app.add_flag("-V,--version", version, "Show program version");

        app.add_option("-v,--verbosity", g_logOptions, "", true)->check(CLI::Range(LOG_NEXT - 1));

        app.add_option("--farm-recheck", m_PoolSettings.getWorkPollInterval, "", true)
            ->check(CLI::Range(1, 99999));

        app.add_option("--farm-retries", m_PoolSettings.connectionMaxRetries, "", true)
            ->check(CLI::Range(0, 99999));

        app.add_option("--retry-delay", m_PoolSettings.delayBeforeRetry, "", true)
            ->check(CLI::Range(1, 999));

        app.add_option("--work-timeout", m_PoolSettings.noWorkTimeout, "", true)
            ->check(CLI::Range(180, 99999));

        app.add_option("--response-timeout", m_PoolSettings.noResponseTimeout, "", true)
            ->check(CLI::Range(2, 999));

        app.add_flag("-R,--report-hashrate,--report-hr", m_PoolSettings.reportHashrate, "");

        app.add_option("--display-interval", m_cliDisplayInterval, "", true)
            ->check(CLI::Range(1, 1800));

        app.add_option("--HWMON", m_FarmSettings.hwMon, "", true)->check(CLI::Range(0, 2));

        app.add_flag("--exit", g_exitOnError, "");

        vector<string> pools;
        app.add_option("-P,--pool", pools, "");

        app.add_option("--failover-timeout", m_PoolSettings.poolFailoverTimeout, "", true)
            ->check(CLI::Range(0, 999));

        app.add_flag("--nocolor", g_logNoColor, "");

        app.add_flag("--syslog", g_logSyslog, "");

#if API_CORE

        app.add_option("--api-bind", m_api_bind, "", true)
            ->check([this](const string& bind_arg) -> string {
                try
                {
                    MinerCLI::ParseBind(bind_arg, this->m_api_address, this->m_api_port, true);
                }
                catch (const exception& ex)
                {
                    throw CLI::ValidationError("--api-bind", ex.what());
                }
                // not sure what to return, and the documentation doesn't say either.
                // https://github.com/CLIUtils/CLI11/issues/144
                return string("");
            });

        app.add_option("--api-port", m_api_port, "", true)->check(CLI::Range(-65535, 65535));

        app.add_option("--api-password", m_api_password, "");
#endif

#if ETH_ETHASHCL || ETH_ETHASHCUDA || ETH_ETHASH_CPU

        app.add_flag("--list-devices", m_shouldListDevices, "");
#endif

        app.add_flag("--eval", m_FarmSettings.eval, "");

        bool cl_miner = false;
        app.add_flag("-G,--opencl", cl_miner, "");

        bool cuda_miner = false;
        app.add_flag("-U,--cuda", cuda_miner, "");

        bool cpu_miner = false;

#if ETH_ETHASHCPU

        app.add_flag("--cpu", cpu_miner, "");
#endif

#if ETH_ETHASHCUDA
        app.add_set("--cu-block-size", m_FarmSettings.cuBlockSize, {32, 64, 128, 256}, "", true);

        app.add_option("--cu-streams", m_FarmSettings.cuStreams, "", true)->check(CLI::Range(1, 4));

#endif

#if ETH_ETHASHCL
        app.add_set("--cl-local-work", m_FarmSettings.clGroupSize, {64, 128, 256}, "", true);

#endif

        auto sim_opt = app.add_option(
            "-Z,--simulation,-M,--benchmark", m_PoolSettings.benchmarkBlock, "", true);

        app.add_option("--tstop", m_FarmSettings.tempStop, "", true)->check(CLI::Range(30, 100));

        app.add_option("--tstart", m_FarmSettings.tempStart, "", true)->check(CLI::Range(30, 100));

        // Exception handling is held at higher level
        app.parse(argc, argv);
        if (bhelp)
        {
            help();
            return false;
        }
        else if (!shelpExt.empty())
        {
            helpExt(shelpExt);
            return false;
        }
        else if (version)
        {
            vector<string> vs;
            headers(vs, false);
            cout << endl;
            for (auto& v : vs)
                cout << v << endl;
            cout << endl;
            return false;
        }

        if (cl_miner)
            m_minerType = MinerType::CL;
        else if (cuda_miner)
            m_minerType = MinerType::CUDA;
        else if (cpu_miner)
            m_minerType = MinerType::CPU;
        else
            m_minerType = MinerType::Mixed;

        /*
            Operation mode Simulation do not require pool definitions
            Operation mode Stratum or GetWork do need at least one
        */

        if (sim_opt->count())
        {
            m_mode = OperationMode::Simulation;
            pools.clear();
            m_PoolSettings.connections.push_back(
                shared_ptr<URI>(new URI("simulation://localhost:0", true)));
        }
        else
        {
            m_mode = OperationMode::Mining;
        }

        if (!m_shouldListDevices && m_mode != OperationMode::Simulation)
        {
            if (!pools.size())
                throw invalid_argument("At least one pool definition required. See -P argument.");

            for (size_t i = 0; i < pools.size(); i++)
            {
                string url = pools.at(i);
                if (url == "exit")
                {
                    if (i == 0)
                        throw invalid_argument(
                            "'exit' failover directive can't be the first in -P arguments list.");
                    else
                        url = "stratum+tcp://-:x@exit:0";
                }

                try
                {
                    shared_ptr<URI> uri = shared_ptr<URI>(new URI(url));
                    if (uri->SecLevel() != dev::SecureLevel::NONE &&
                        uri->HostNameType() != dev::UriHostNameType::Dns && !getenv("SSL_NOVERIFY"))
                    {
                        warnings.push(
                            "You have specified host " + uri->Host() + " with encryption enabled.");
                        warnings.push("Certificate validation will likely fail");
                    }
                    m_PoolSettings.connections.push_back(uri);
                }
                catch (const exception& _ex)
                {
                    string what = _ex.what();
                    throw runtime_error("Bad URI : " + what);
                }
            }
        }


        if (m_FarmSettings.tempStop)
        {
            // If temp threshold set HWMON at least to 1
            m_FarmSettings.hwMon = max((unsigned int)m_FarmSettings.hwMon, 1U);
            if (m_FarmSettings.tempStop <= m_FarmSettings.tempStart)
            {
                string what = "-tstop must be greater than -tstart";
                throw invalid_argument(what);
            }
        }

        // Output warnings if any
        if (warnings.size())
        {
            while (warnings.size())
            {
                cout << warnings.front() << endl;
                warnings.pop();
            }
            cout << endl;
        }
        return true;
    }

    void execute()
    {
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
        if (m_shouldListDevices)
        {
            cout << setw(4) << " Id ";
            cout << setiosflags(ios::left) << setw(10) << "Pci Id    ";
            cout << setw(5) << "Type ";
            cout << setw(30) << "Name                          ";

#if ETH_ETHASHCUDA
            if (m_minerType == MinerType::CUDA || m_minerType == MinerType::Mixed)
            {
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
#if ETH_ETHASHCL
            if (m_minerType == MinerType::CL || m_minerType == MinerType::Mixed)
            {
                cout << resetiosflags(ios::left) << setw(13) << "Cl Max Alloc"
                     << " ";
                cout << resetiosflags(ios::left) << setw(13) << "Cl Max W.Grp"
                     << " ";
            }
#endif

            cout << resetiosflags(ios::left) << endl;
            cout << setw(4) << "--- ";
            cout << setiosflags(ios::left) << setw(10) << "--------- ";
            cout << setw(5) << "---- ";
            cout << setw(30) << "----------------------------- ";

#if ETH_ETHASHCUDA
            if (m_minerType == MinerType::CUDA || m_minerType == MinerType::Mixed)
            {
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
#if ETH_ETHASHCL
            if (m_minerType == MinerType::CL || m_minerType == MinerType::Mixed)
            {
                cout << resetiosflags(ios::left) << setw(13) << "------------"
                     << " ";
                cout << resetiosflags(ios::left) << setw(13) << "------------"
                     << " ";
            }
#endif
            cout << resetiosflags(ios::left) << endl;
            map<string, DeviceDescriptor>::iterator it = m_DevicesCollection.begin();
            while (it != m_DevicesCollection.end())
            {
                auto i = distance(m_DevicesCollection.begin(), it);
                cout << setw(3) << i << " ";
                cout << setiosflags(ios::left) << setw(10) << it->first;
                cout << setw(5);
                switch (it->second.type)
                {
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
                cout << setw(30) << (it->second.name).substr(0, 28);
#if ETH_ETHASHCUDA
                if (m_minerType == MinerType::CUDA || m_minerType == MinerType::Mixed)
                {
                    cout << setw(5) << (it->second.cuDetected ? "Yes" : "");
                    cout << setw(4) << it->second.cuCompute;
                }
#endif
#if ETH_ETHASHCL
                if (m_minerType == MinerType::CL || m_minerType == MinerType::Mixed)
                    cout << setw(5) << (it->second.clDetected ? "Yes" : "");
#endif
                cout << resetiosflags(ios::left) << setw(13)
                     << getFormattedMemory((double)it->second.totalMemory) << " ";
                cout << resetiosflags(ios::left) << endl;
                it++;
            }

            return;
        }

        // Subscribe devices with appropriate Miner Type
        // Use CUDA first when available then, as second, OpenCL

        // Subscribe all detected devices
#if ETH_ETHASHCUDA
        if (m_minerType == MinerType::CUDA || m_minerType == MinerType::Mixed)
        {
            for (auto it = m_DevicesCollection.begin(); it != m_DevicesCollection.end(); it++)
            {
                if (!it->second.cuDetected ||
                    it->second.subscriptionType != DeviceSubscriptionTypeEnum::None)
                    continue;
                it->second.subscriptionType = DeviceSubscriptionTypeEnum::Cuda;
            }
        }
#endif
#if ETH_ETHASHCL
        if (m_minerType == MinerType::CL || m_minerType == MinerType::Mixed)
        {
            for (auto it = m_DevicesCollection.begin(); it != m_DevicesCollection.end(); it++)
            {
                if (!it->second.clDetected ||
                    it->second.subscriptionType != DeviceSubscriptionTypeEnum::None)
                    continue;
                it->second.subscriptionType = DeviceSubscriptionTypeEnum::OpenCL;
            }
        }
#endif
#if ETH_ETHASHCPU
        if (m_minerType == MinerType::CPU)
        {
            for (auto it = m_DevicesCollection.begin(); it != m_DevicesCollection.end(); it++)
            {
                it->second.subscriptionType = DeviceSubscriptionTypeEnum::Cpu;
            }
        }
#endif
        // Count of subscribed devices
        int subscribedDevices = 0;
        for (auto it = m_DevicesCollection.begin(); it != m_DevicesCollection.end(); it++)
        {
            if (it->second.subscriptionType != DeviceSubscriptionTypeEnum::None)
                subscribedDevices++;
        }

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

    void help()
    {
        cout << "\nnsfminer - GPU ethash miner\n\n"
             << "minimal usage : nsfminer [DEVICES_TYPE] [OPTIONS] -P... [-P...]\n\n"
             << "Devices type options :\n\n"
             << "    By default the miner will try to use all devices types\n"
             << "    it can detect. Optionally you can limit this behavior\n"
             << "    setting either of the following options\n\n"
#if ETH_ETHASHCL
             << "    -G,--opencl         Mine/Benchmark using OpenCL only\n"
#endif
#if ETH_ETHASHCUDA
             << "    -U,--cuda           Mine/Benchmark using CUDA only\n"
#endif
#if ETH_ETHASHCPU
             << "    --cpu               Development ONLY ! (NO MINING)\n"
#endif
             << "\nConnection options :\n\n"
             << "    -P,--pool           Stratum pool or http (getWork) connection as URL\n"
             << "                        "
                "scheme://[user[.workername][:password]@]hostname:port[/...]\n"
             << "                        For an explication and some samples about\n"
             << "                        how to fill in this value please use\n"
             << "                        nsfminer --help-ext con\n\n"
             << "Common Options :\n\n"
             << "    -h,--help           Displays this help text and exits\n"
             << "    -H,--help-ext       TEXT {'con','test',"
#if ETH_ETHASHCL
             << "cl,"
#endif
#if ETH_ETHASHCUDA
             << "cu,"
#endif
#if ETH_ETHASHCPU
             << "cp,"
#endif
#if API_CORE
             << "api,"
#endif
             << "'misc'}\n"
             << "                        Display help text about one of these contexts:\n"
             << "                        'con'  Connections and their definitions\n"
             << "                        'test' Benchmark/Simulation options\n"
#if ETH_ETHASHCL
             << "                        'cl'   Extended OpenCL options\n"
#endif
#if ETH_ETHASHCUDA
             << "                        'cu'   Extended CUDA options\n"
#endif
#if ETH_ETHASHCPU
             << "                        'cp'   Extended CPU options\n"
#endif
#if API_CORE
             << "                        'api'  API and Http monitoring interface\n"
#endif
             << "                        'misc' Other miscellaneous options\n"
             << "    -V,--version        Show program version and exits\n\n";
    }

    void helpExt(string ctx)
    {
        // Help text for benchmarking options
        if (ctx == "test")
        {
            cout << "\nBenchmarking / Simulation options :\n\n"
                 << "    When playing with benchmark or simulation no connection specification "
                    "is\n"
                 << "    needed ie. you can omit any -P argument.\n\n"
                 << "    -M,--benchmark      UINT [0 ..] Default not set\n"
                 << "                        Mining test. Used to test hashing speed.\n"
                 << "                        Specify the block number to test on.\n\n"
                 << "    -Z,--simulation     UINT [0 ..] Default not set\n"
                 << "                        Mining test. Used to test hashing speed.\n"
                 << "                        Specify the block number to test on.\n\n";
        }

        // Help text for API interfaces options
        if (ctx == "api")
        {
            cout << "\nAPI Interface Options :\n\n"
                 << "    Ethminer provide an interface for monitor and or control\n"
                 << "    Please note that information delivered by API interface\n"
                 << "    may depend on value of --HWMON\n"
                 << "    A single endpoint is used to accept both HTTP or plain tcp\n"
                 << "    requests.\n\n"
                 << "    --api-bind          TEXT Default not set\n"
                 << "                        Set the API address:port the miner should listen "
                    "on. \n"
                 << "                        Use negative port number for readonly mode\n"
                 << "    --api-port          INT [1 .. 65535] Default not set\n"
                 << "                        Set the API port, the miner should listen on all "
                    "bound\n"
                 << "                        addresses. Use negative numbers for readonly mode\n"
                 << "    --api-password      TEXT Default not set\n"
                 << "                        Set the password to protect interaction with API "
                    "server. \n"
                 << "                        If not set, any connection is granted access.\n"
                 << "                        Be advised passwords are sent unencrypted over "
                 << "plain TCP!!\n\n";
        }

        if (ctx == "cl")
        {
            cout << "\nOpenCL Extended Options :\n\n"
                 << "    Use this extended OpenCL arguments to fine tune the performance.\n"
                 << "    Be advised default values are best generic findings by developers\n\n"
                 << "    --cl-local-work     UINT {64,128,256} Default = 128\n"
                 << "                        Set the local work size multiplier\n\n";
        }

        if (ctx == "cu")
        {
            cout << "\nCUDA Extended Options :\n\n"
                 << "    Use this extended CUDA arguments to fine tune the performance.\n"
                 << "    Be advised default values are best generic findings by developers\n\n"
                 << "    --cu-block-size     UINT {32,64,128,256} Default = 128\n"
                 << "                        Set the block size\n\n"
                 << "    --cu-streams        INT [1 .. 4] Default = 2\n"
                 << "                        Set the number of streams per GPU\n\n";
        }

        if (ctx == "cp")
        {
            cout << "\nCPU Extended Options :\n\n"
                 << "    Use this extended CPU arguments\n\n"
                 << "    --cp-devices        UINT {} Default not set\n"
                 << "                        Space separated list of device indexes to use\n"
                 << "                        eg --cp-devices 0 2 3\n"
                 << "                        If not set all available CPUs will be used\n\n";
        }

        if (ctx == "misc")
        {
            cout << "\nMiscellaneous Options :\n\n"
                 << "    This set of options is valid for mining mode independently from\n"
                 << "    OpenCL or CUDA or Mixed mining mode.\n\n"
                 << "    --display-interval  INT[1 .. 1800] Default = 5\n"
                 << "                        Statistic display interval in seconds\n"
                 << "    --farm-recheck      INT[1 .. 99999] Default = 500\n"
                 << "                        Set polling interval for new work in getWork mode\n"
                 << "                        Value expressed in milliseconds\n"
                 << "                        It has no meaning in stratum mode\n"
                 << "    --farm-retries      INT[1 .. 99999] Default = 3\n"
                 << "                        Set number of reconnection retries to same pool\n"
                 << "    --retry-delay       INT[1 .. 999] Default = 0\n"
                 << "                        Delay in seconds before reconnection retry\n"
                 << "    --failover-timeout  INT[0 .. ] Default not set\n"
                 << "                        Sets the number of minutes miner can stay\n"
                 << "                        connected to a fail-over pool before trying to\n"
                 << "                        reconnect to the primary (the first) connection.\n"
                 << "                        before switching to a fail-over connection\n"
                 << "    --work-timeout      INT[180 .. 99999] Default = 180\n"
                 << "                        If no new work received from pool after this\n"
                 << "                        amount of time the connection is dropped\n"
                 << "                        Value expressed in seconds.\n"
                 << "    --response-timeout  INT[2 .. 999] Default = 2\n"
                 << "                        If no response from pool to a stratum message \n"
                 << "                        after this amount of time the connection is dropped\n"
                 << "    -R,--report-hr      FLAG Notify pool of effective hashing rate\n"
                 << "    --HWMON             INT[0 .. 2] Default = 0\n"
                 << "                        GPU hardware monitoring level. Can be one of:\n"
                 << "                        0 No monitoring\n"
                 << "                        1 Monitor temperature and fan percentage\n"
                 << "                        2 As 1 plus monitor power drain\n"
                 << "    --exit              FLAG Stop miner whenever an error is encountered\n"
                 << "    --nocolor           FLAG Monochrome display log lines\n"
                 << "    --syslog            FLAG Use syslog appropriate output (drop timestamp "
                    "and\n"
                 << "                        channel prefix)\n"
                 << "    --eval              FLAG Enable host software re-evaluation of GPUs\n"
                 << "                        found nonces. Trims some ms. from submission\n"
                 << "                        time but it may increase rejected solution rate.\n"
                 << "    --list-devices      FLAG Lists the detected OpenCL/CUDA devices and "
                    "exits\n"
                 << "                        Must be combined with -G or -U or -X flags\n"
                 << "    --tstart            UINT[30 .. 100] Default = 0\n"
                 << "                        Suspend mining on GPU which temperature is above\n"
                 << "                        this threshold. Implies --HWMON 1\n"
                 << "                        If not set or zero no temp control is performed\n"
                 << "    --tstop             UINT[30 .. 100] Default = 40\n"
                 << "                        Resume mining on previously overheated GPU when "
                    "temp\n"
                 << "                        drops below this threshold. Implies --HWMON 1\n"
                 << "                        Must be lower than --tstart\n"
                 << "    -v,--verbosity      INT[0 .. 255] Default = 0\n"
                 << "                        Set output verbosity level. Use the sum of :\n"
                 << "                        1   to log stratum json messages\n"
                 << "                        2   to log found solutions per GPU\n"
#ifdef DEV_BUILD
                 << "                        32  to log socket (dis)connections\n"
                 << "                        64  to log timing of job switches\n"
                 << "                        128 to log time for solution submission\n"
#endif
                 << '\n';
        }

        if (ctx == "env")
        {
            cout << "Environment variables :\n\n"
                 << "    If you need or do feel more comfortable you can set the following\n"
                 << "    environment variables. Please respect letter casing.\n\n"
#ifndef _WIN32
                 << "    SSL_CERT_FILE       Set to the full path to of your CA certificates "
                    "file\n"
                 << "                        if it is not in standard path :\n"
                 << "                        /etc/ssl/certs/ca-certificates.crt.\n"
#endif
                 << "    SSL_NOVERIFY        set to any value to to disable the verification "
                    "chain for\n"
                 << "                        certificates. WARNING ! Disabling certificate "
                    "validation\n"
                 << "                        declines every security implied in connecting to a "
                    "secured\n"
                 << "                        SSL/TLS remote endpoint.\n"
                 << "                        USE AT YOU OWN RISK AND ONLY IF YOU KNOW WHAT "
                    "YOU'RE DOING\n\n";
        }

        if (ctx == "con")
        {
            cout << "\nConnections specifications :\n\n"
                 << "    Whether you need to connect to a stratum pool or to make use of "
                    "getWork polling\n"
                 << "    mode (generally used to solo mine) you need to specify the connection "
                    "making use\n"
                 << "    of -P command line argument filling up the URL. The URL is in the form "
                    ":\n\n"
                 << "    scheme://[user[.workername][:password]@]hostname:port[/...].\n\n"
                 << "    where 'scheme' can be any of :\n\n"
                 << "    getwork    for http getWork mode\n"
                 << "    stratum    for tcp stratum mode\n"
                 << "    stratums   for tcp encrypted stratum mode\n"
                 << "    stratumss  for tcp encrypted stratum mode with strong TLS 1.2 "
                    "validation\n\n"
                 << "    Example 1: -P getwork://127.0.0.1:8545\n"
                 << "    Example 2: "
                    "-P stratums://0x012345678901234567890234567890123.miner1@ethermine.org:5555\n"
                 << "    Example 3: "
                    "-P stratum://0x012345678901234567890234567890123.miner1@nanopool.org:9999/"
                    "john.doe%40gmail.com\n"
                 << "    Example 4: "
                    "-P stratum://0x012345678901234567890234567890123@nanopool.org:9999/miner1/"
                    "john.doe%40gmail.com\n\n"
                 << "    Please note: if your user or worker or password do contain characters\n"
                 << "    which may impair the correct parsing (namely any of . : @ # ?) you have "
                    "to\n"
                 << "    enclose those values in backticks( ` ASCII 096) or Url Encode them\n"
                 << "    Also note that backtick has a special meaning in *nix environments thus\n"
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
                 << "    where a scheme is made up of two parts, the stratum variant + the tcp "
                    "transport protocol\n"
                 << "    Stratum variants :\n\n"
                 << "        stratum     Stratum\n"
                 << "        stratum1    Eth Proxy compatible\n"
                 << "        stratum2    EthereumStratum 1.0.0 (nicehash)\n"
                 << "        stratum3    EthereumStratum 2.0.0\n\n"
                 << "    Transport variants :\n\n"
                 << "        tcp         Unencrypted tcp connection\n"
                 << "        tls         Encrypted tcp connection (including deprecated TLS 1.1)\n"
                 << "        tls12       Encrypted tcp connection with TLS 1.2\n"
                 << "        ssl         Encrypted tcp connection with TLS 1.2\n\n";
        }
    }

private:
    void doMiner()
    {
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
        m_cliDisplayTimer.async_wait(m_io_strand.wrap(boost::bind(
            &MinerCLI::cliDisplayInterval_elapsed, this, boost::asio::placeholders::error)));

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
    thread m_io_thread;                             // The IO service thread
    boost::asio::deadline_timer m_cliDisplayTimer;  // The timer which ticks display lines
    boost::asio::io_service::strand m_io_strand;    // A strand to serialize posts in
                                                    // multithreaded environment

    // Physical Mining Devices descriptor
    map<string, DeviceDescriptor> m_DevicesCollection = {};

    // Mining options
    MinerType m_minerType = MinerType::Mixed;
    OperationMode m_mode = OperationMode::None;
    bool m_shouldListDevices = false;

    FarmSettings m_FarmSettings;  // Operating settings for Farm
    PoolSettings m_PoolSettings;  // Operating settings for PoolManager

    //// -- Pool manager related params
    // vector<shared_ptr<URI>> m_poolConns;


    // -- CLI Interface related params
    unsigned m_cliDisplayInterval =
        5;  // Display stats/info on cli interface every this number of seconds

    // -- CLI Flow control
    mutex m_climtx;

#if API_CORE
    // -- API and Http interfaces related params
    string m_api_bind;                 // API interface binding address in form <address>:<port>
    string m_api_address = "0.0.0.0";  // API interface binding address (Default any)
    int m_api_port = 0;                // API interface binding port
    string m_api_password;             // API interface write protection password
#endif

#if ETH_DBUS
    DBusInt dbusint;
#endif
};

int main(int argc, char** argv)
{
    // Return values
    // 0 - Normal exit
    // 1 - Invalid/Insufficient command line arguments
    // 2 - Runtime error
    // 3 - Other exceptions
    // 4 - Possible corruption

#if defined(_WIN32)
    // Need to change the code page from the default OEM code page (437) so that
    // UTF-8 characters are displayed correctly in the console
    SetConsoleOutputCP(CP_UTF8);
#endif

    dev::setThreadName("miner");

#if defined(_WIN32)
    {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut != INVALID_HANDLE_VALUE)
        {
            DWORD dwMode;
            if (GetConsoleMode(hOut, &dwMode))
                SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    }
#endif

    if (argc < 2)
    {
        cout << "No arguments specified.";
        cout << "Try 'nsfminer --help' to get a list of arguments.";
        return 1;
    }

    try
    {
        MinerCLI cli;

        try
        {
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

            vector<string> vs;
            headers(vs, !g_logNoColor);
            for (auto& v : vs)
                cnote << v;


            cli.execute();
            cout << endl << endl;
            return 0;
        }
        catch (invalid_argument& ex1)
        {
            cout << "\nError: " << ex1.what()
                 << "\nTry nsfminer --help to get an explained list of arguments.\n\n";
            return 1;
        }
        catch (runtime_error& ex2)
        {
            cout << "\nError: " << ex2.what() << "\n\n";
            return 2;
        }
        catch (exception& ex3)
        {
            cout << "\nError: " << ex3.what() << "\n\n";
            return 3;
        }
        catch (...)
        {
            cout << "\n\nError: Unknown failure occurred. Possible memory corruption.\n\n";
            return 4;
        }
    }
    catch (const exception& ex)
    {
        cout << "Could not initialize CLI interface " << endl << "Error: " << ex.what();
        return 4;
    }
}
