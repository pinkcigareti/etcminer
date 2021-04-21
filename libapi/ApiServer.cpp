
/* Copyright (C) 1883 Thomas Edison - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPLv3 license, which unfortunately won't be
 * written for another century.
 *
 * You should have received a copy of the LICENSE file with
 * this file.
 */

#include "ApiServer.h"

#include <nsfminer/buildinfo.h>

#include <libeth/Farm.h>

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 255
#endif

// Define grayscale palette

#define HTTP_HDR0_COLOR "#e8e8e8"
#define HTTP_HDR1_COLOR "#f0f0f0"
#define HTTP_ROW0_COLOR "#f8f8f8"
#define HTTP_ROW1_COLOR "#ffffff"
#define HTTP_ROWRED_COLOR "#f46542"

using namespace std;

/* helper functions getting values from a JSON request */
static bool getRequestValue(const char* membername, bool& refValue, Json::Value& jRequest, bool optional,
                            Json::Value& jResponse) {
    if (!jRequest.isMember(membername)) {
        if (!optional) {
            jResponse["error"]["code"] = -32602;
            jResponse["error"]["message"] = string("Missing '") + string(membername) + string("'");
        }
        return optional;
    }
    if (!jRequest[membername].isBool()) {
        jResponse["error"]["code"] = -32602;
        jResponse["error"]["message"] = string("Invalid type of value '") + string(membername) + string("'");
        return false;
    }
    if (jRequest[membername].empty()) {
        jResponse["error"]["code"] = -32602;
        jResponse["error"]["message"] = string("Empty '") + string(membername) + string("'");
        return false;
    }
    refValue = jRequest[membername].asBool();
    return true;
}

static bool getRequestValue(const char* membername, unsigned& refValue, Json::Value& jRequest, bool optional,
                            Json::Value& jResponse) {
    if (!jRequest.isMember(membername)) {
        if (!optional) {
            jResponse["error"]["code"] = -32602;
            jResponse["error"]["message"] = string("Missing '") + string(membername) + string("'");
        }
        return optional;
    }
    if (!jRequest[membername].isUInt()) {
        jResponse["error"]["code"] = -32602;
        jResponse["error"]["message"] = string("Invalid type of value '") + string(membername) + string("'");
        return false;
    }
    if (jRequest[membername].empty()) {
        jResponse["error"]["code"] = -32602;
        jResponse["error"]["message"] = string("Empty '") + string(membername) + string("'");
        return false;
    }
    refValue = jRequest[membername].asUInt();
    return true;
}

static bool getRequestValue(const char* membername, Json::Value& refValue, Json::Value& jRequest, bool optional,
                            Json::Value& jResponse) {
    if (!jRequest.isMember(membername)) {
        if (!optional) {
            jResponse["error"]["code"] = -32602;
            jResponse["error"]["message"] = string("Missing '") + string(membername) + string("'");
        }
        return optional;
    }
    if (!jRequest[membername].isObject()) {
        jResponse["error"]["code"] = -32602;
        jResponse["error"]["message"] = string("Invalid type of value '") + string(membername) + string("'");
        return false;
    }
    if (jRequest[membername].empty()) {
        jResponse["error"]["code"] = -32602;
        jResponse["error"]["message"] = string("Empty '") + string(membername) + string("'");
        return false;
    }
    refValue = jRequest[membername];
    return true;
}

static bool getRequestValue(const char* membername, string& refValue, Json::Value& jRequest, bool optional,
                            Json::Value& jResponse) {
    if (!jRequest.isMember(membername)) {
        if (!optional) {
            jResponse["error"]["code"] = -32602;
            jResponse["error"]["message"] = string("Missing '") + string(membername) + string("'");
        }
        return optional;
    }
    if (!jRequest[membername].isString()) {
        jResponse["error"]["code"] = -32602;
        jResponse["error"]["message"] = string("Invalid type of value '") + string(membername) + string("'");
        return false;
    }
    if (jRequest[membername].empty()) {
        jResponse["error"]["code"] = -32602;
        jResponse["error"]["message"] = string("Empty '") + string(membername) + string("'");
        return false;
    }
    refValue = jRequest[membername].asString();
    return true;
}

static bool checkApiWriteAccess(bool is_read_only, Json::Value& jResponse) {
    if (is_read_only) {
        jResponse["error"]["code"] = -32601;
        jResponse["error"]["message"] = "Method not available";
    }
    return !is_read_only;
}

static bool parseRequestId(Json::Value& jRequest, Json::Value& jResponse) {
    const char* membername = "id";

    // NOTE: all errors have the same code (-32600) indicating this is an invalid request

    // be sure id is there and it's not empty, otherwise raise an error
    if (!jRequest.isMember(membername) || jRequest[membername].empty()) {
        jResponse[membername] = Json::nullValue;
        jResponse["error"]["code"] = -32600;
        jResponse["error"]["message"] = "Invalid Request (missing or empty id)";
        return false;
    }

    // try to parse id as Uint
    if (jRequest[membername].isUInt()) {
        jResponse[membername] = jRequest[membername].asUInt();
        return true;
    }

    // try to parse id as String
    if (jRequest[membername].isString()) {
        jResponse[membername] = jRequest[membername].asString();
        return true;
    }

    // id has invalid type
    jResponse[membername] = Json::nullValue;
    jResponse["error"]["code"] = -32600;
    jResponse["error"]["message"] = "Invalid Request (id has invalid type)";
    return false;
}

ApiServer::ApiServer(string address, int portnum, string password)
    : m_password(move(password)), m_address(address), m_acceptor(g_io_service), m_io_strand(g_io_service) {
    if (portnum < 0) {
        m_portnumber = -portnum;
        m_readonly = true;
    } else {
        m_portnumber = portnum;
        m_readonly = false;
    }
}

void ApiServer::start() {
    // cnote << "ApiServer::start";
    if (m_portnumber == 0)
        return;

    tcp::endpoint endpoint(boost::asio::ip::address::from_string(m_address), m_portnumber);

    // Try to bind to port number
    // if exception occurs it may be due to the fact that
    // requested port is already in use by another service
    try {
        m_acceptor.open(endpoint.protocol());
        m_acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
        m_acceptor.bind(endpoint);
        m_acceptor.listen(64);
    } catch (const exception&) {
        cwarn << "Could not start API server on port: " + to_string(m_acceptor.local_endpoint().port());
        cwarn << "Ensure port is not in use by another service";
        return;
    }

    cnote << "Api server listening on port " + to_string(m_acceptor.local_endpoint().port())
          << (m_password.empty() ? "." : ". Authentication needed.");
    m_running.store(true, memory_order_relaxed);
    m_workThread = thread{boost::bind(&ApiServer::begin_accept, this)};
}

void ApiServer::stop() {
    // Exit if not started
    if (!m_running.load(memory_order_relaxed))
        return;

    m_acceptor.cancel();
    m_acceptor.close();
    m_workThread.join();
    m_running.store(false, memory_order_relaxed);

    // Dispose all sessions (if any)
    m_sessions.clear();
}

void ApiServer::begin_accept() {
    if (!isRunning())
        return;

    auto session = make_shared<ApiConnection>(m_io_strand, ++lastSessionId, m_readonly, m_password);
    m_acceptor.async_accept(session->socket(), m_io_strand.wrap(boost::bind(&ApiServer::handle_accept, this, session,
                                                                            boost::asio::placeholders::error)));
}

void ApiServer::handle_accept(shared_ptr<ApiConnection> session, boost::system::error_code ec) {
    // Start new connection
    // cnote << "ApiServer::handle_accept";
    if (!ec) {
        session->onDisconnected([&](int id) {
            // Destroy pointer to session
            auto it = find_if(m_sessions.begin(), m_sessions.end(),
                              [&id](const shared_ptr<ApiConnection> session) { return session->getId() == id; });
            if (it != m_sessions.end()) {
                auto index = distance(m_sessions.begin(), it);
                m_sessions.erase(m_sessions.begin() + index);
            }
        });
        m_sessions.push_back(session);
        cnote << "New API session from " << session->socket().remote_endpoint();
        session->start();
    } else {
        session.reset();
    }

    // Resubmit new accept
    begin_accept();
}

void ApiConnection::disconnect() {
    // cnote << "ApiConnection::disconnect";

    // Cancel pending operations
    m_socket.cancel();

    if (m_socket.is_open()) {
        boost::system::error_code ec;
        m_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        m_socket.close(ec);
    }

    if (m_onDisconnected) {
        m_onDisconnected(this->getId());
    }
}

ApiConnection::ApiConnection(boost::asio::io_service::strand& _strand, int id, bool readonly, string password)
    : m_sessionId(id), m_socket(g_io_service), m_io_strand(_strand), m_readonly(readonly), m_password(move(password)) {
    m_jSwBuilder.settings_["indentation"] = "";
    if (!m_password.empty())
        m_is_authenticated = false;
}

void ApiConnection::start() {
    // cnote << "ApiConnection::start";
    recvSocketData();
}

void ApiConnection::processRequest(Json::Value& jRequest, Json::Value& jResponse) {
    jResponse["jsonrpc"] = "2.0";

    // Strict sanity checks over jsonrpc v2
    if (!parseRequestId(jRequest, jResponse))
        return;

    string jsonrpc;
    string _method;
    if (!getRequestValue("jsonrpc", jsonrpc, jRequest, false, jResponse) || jsonrpc != "2.0" ||
        !getRequestValue("method", _method, jRequest, false, jResponse)) {
        jResponse["error"]["code"] = -32600;
        jResponse["error"]["message"] = "Invalid Request";
        return;
    }

    // Check authentication
    if (!m_is_authenticated || _method == "api_authorize") {
        if (_method != "api_authorize") {
            // Use error code like http 403 Forbidden
            jResponse["error"]["code"] = -403;
            jResponse["error"]["message"] = "Authorization needed";
            return;
        }

        m_is_authenticated = false; /* we allow api_authorize method even if already authenticated */

        Json::Value jRequestParams;
        if (!getRequestValue("params", jRequestParams, jRequest, false, jResponse))
            return;

        string psw;
        if (!getRequestValue("psw", psw, jRequestParams, false, jResponse))
            return;

        // max password length that we actually verify
        // (this limit can be removed by introducing a collision-resistant compressing hash,
        //  like blake2b/sha3, but 500 should suffice and is much easier to implement)
        const int max_length = 500;
        char input_copy[max_length] = {0};
        char password_copy[max_length] = {0};
        // note: copy() is not O(1) , but i don't think it matters
        psw.copy(&input_copy[0], max_length);
        // ps, the following line can be optimized to only run once on startup and thus save a
        // minuscule amount of cpu cycles.
        m_password.copy(&password_copy[0], max_length);
        int result = 0;
        for (int i = 0; i < max_length; ++i)
            result |= input_copy[i] ^ password_copy[i];

        if (result == 0)
            m_is_authenticated = true;
        else {
            // Use error code like http 401 Unauthorized
            jResponse["error"]["code"] = -401;
            jResponse["error"]["message"] = "Invalid password";
            cwarn << "API : Invalid password provided.";
            // Should we close the connection in the outer function after invalid password ?
        }
        /*
         * possible wait here a fixed time of eg 10s before respond after 5 invalid
           authentications were submitted to prevent brute force password attacks.
        */
        return;
    }

    cnote << "API : Method " << _method << " requested";
    if (_method == "miner_getstat1") {
        jResponse["result"] = getMinerStat1();
    }

    else if (_method == "miner_getstatdetail") {
        jResponse["result"] = getMinerStatDetail();
    }

    else if (_method == "miner_ping") {
        // Replies back to (check for liveness)
        jResponse["result"] = "pong";
    }

    else if (_method == "miner_restart") {
        // Send response to client of success
        // and invoke an async restart
        // to prevent locking
        if (!checkApiWriteAccess(m_readonly, jResponse))
            return;
        jResponse["result"] = true;
        Farm::f().restart_async();
    }

    else if (_method == "miner_reboot") {
        if (!checkApiWriteAccess(m_readonly, jResponse))
            return;

        jResponse["result"] = Farm::f().reboot({{"api_miner_reboot"}});
    }

    else if (_method == "miner_getconnections") {
        // Returns a list of configured pools
        jResponse["result"] = PoolManager::p().getConnectionsJson();
    }

    else if (_method == "miner_addconnection") {
        if (!checkApiWriteAccess(m_readonly, jResponse))
            return;

        Json::Value jRequestParams;
        if (!getRequestValue("params", jRequestParams, jRequest, false, jResponse))
            return;

        string sUri;
        if (!getRequestValue("uri", sUri, jRequestParams, false, jResponse))
            return;

        try {
            // If everything ok then add this new uri
            PoolManager::p().addConnection(sUri);
            jResponse["result"] = true;
        } catch (...) {
            jResponse["error"]["code"] = -422;
            jResponse["error"]["message"] = "Bad URI : " + sUri;
        }
    }

    else if (_method == "miner_setactiveconnection") {
        if (!checkApiWriteAccess(m_readonly, jResponse))
            return;

        Json::Value jRequestParams;
        if (!getRequestValue("params", jRequestParams, jRequest, false, jResponse))
            return;
        if (jRequestParams.isMember("index")) {
            unsigned index;
            if (getRequestValue("index", index, jRequestParams, false, jResponse)) {
                try {
                    PoolManager::p().setActiveConnection(index);
                } catch (const exception& _ex) {
                    string what = _ex.what();
                    jResponse["error"]["code"] = -422;
                    jResponse["error"]["message"] = what;
                    return;
                }
            } else {
                jResponse["error"]["code"] = -422;
                jResponse["error"]["message"] = "Invalid index";
                return;
            }
        } else {
            string uri;
            if (getRequestValue("URI", uri, jRequestParams, false, jResponse)) {
                try {
                    PoolManager::p().setActiveConnection(uri);
                } catch (const exception& _ex) {
                    string what = _ex.what();
                    jResponse["error"]["code"] = -422;
                    jResponse["error"]["message"] = what;
                    return;
                }
            } else {
                jResponse["error"]["code"] = -422;
                jResponse["error"]["message"] = "Invalid index";
                return;
            }
        }
        jResponse["result"] = true;
    }

    else if (_method == "miner_removeconnection") {
        if (!checkApiWriteAccess(m_readonly, jResponse))
            return;

        Json::Value jRequestParams;
        if (!getRequestValue("params", jRequestParams, jRequest, false, jResponse))
            return;

        unsigned index;
        if (!getRequestValue("index", index, jRequestParams, false, jResponse))
            return;

        try {
            PoolManager::p().removeConnection(index);
            jResponse["result"] = true;
        } catch (const exception& _ex) {
            string what = _ex.what();
            jResponse["error"]["code"] = -422;
            jResponse["error"]["message"] = what;
            return;
        }
    }

    else if (_method == "miner_pausegpu") {
        if (!checkApiWriteAccess(m_readonly, jResponse))
            return;

        Json::Value jRequestParams;
        if (!getRequestValue("params", jRequestParams, jRequest, false, jResponse))
            return;

        unsigned index;
        if (!getRequestValue("index", index, jRequestParams, false, jResponse))
            return;

        bool pause;
        if (!getRequestValue("pause", pause, jRequestParams, false, jResponse))
            return;

        auto const& miner = Farm::f().getMiner(index);
        if (miner) {
            if (pause)
                miner->pause(MinerPauseEnum::PauseDueToAPIRequest);
            else
                miner->resume(MinerPauseEnum::PauseDueToAPIRequest);

            jResponse["result"] = true;
        } else {
            jResponse["error"]["code"] = -422;
            jResponse["error"]["message"] = "Index out of bounds";
            return;
        }
    }

    else if (_method == "miner_setverbosity") {
        if (!checkApiWriteAccess(m_readonly, jResponse))
            return;

        Json::Value jRequestParams;
        if (!getRequestValue("params", jRequestParams, jRequest, false, jResponse))
            return;

        unsigned verbosity;
        if (!getRequestValue("verbosity", verbosity, jRequestParams, false, jResponse))
            return;

        if (verbosity >= LOG_NEXT) {
            jResponse["error"]["code"] = -422;
            jResponse["error"]["message"] = "Verbosity out of bounds (0-" + to_string(LOG_NEXT - 1) + ")";
            return;
        }
        cnote << "Setting verbosity level to " << verbosity;
        g_logOptions = verbosity;
        jResponse["result"] = true;
    }

    else if (_method == "miner_setnonce") {
        if (!checkApiWriteAccess(m_readonly, jResponse))
            return;

        Json::Value jRequestParams;
        if (!getRequestValue("params", jRequestParams, jRequest, false, jResponse))
            return;
        if (jRequestParams.isMember("nonce")) {
            string nonce;
            if (getRequestValue("nonce", nonce, jRequestParams, false, jResponse)) {
                for (const auto& c : nonce)
                    if ((c < '0' || c > '9') && (c < 'a' || c > 'f') && (c < 'A' || c > 'F')) {
                        jResponse["error"]["code"] = -422;
                        jResponse["error"]["message"] = "Invalid nonce";
                        return;
                    }
                cnote << "API: Setting start nonce to '" << nonce << "'";
                Farm::f().set_nonce(nonce);
            } else {
                jResponse["error"]["code"] = -422;
                jResponse["error"]["message"] = "Invalid nonce";
                return;
            }
        }
        jResponse["result"] = true;
    }

    else if (_method == "miner_getnonce") {
        jResponse["result"] = Farm::f().get_nonce();
    }

    else {
        // Any other method not found
        jResponse["error"]["code"] = -32601;
        jResponse["error"]["message"] = "Method not found";
    }
}

void ApiConnection::recvSocketData() {
    boost::asio::async_read(
        m_socket, m_recvBuffer, boost::asio::transfer_at_least(1),
        m_io_strand.wrap(boost::bind(&ApiConnection::onRecvSocketDataCompleted, this, boost::asio::placeholders::error,
                                     boost::asio::placeholders::bytes_transferred)));
}

void ApiConnection::onRecvSocketDataCompleted(const boost::system::error_code& ec, size_t bytes_transferred) {
    /*
    Standard http request detection pattern
    1st group : any UPPERCASE word
    2nd group : the path
    3rd group : HTTP version
    */
    static regex http_pattern("^([A-Z]{1,6}) (\\/[\\S]*) (HTTP\\/1\\.[0-9]{1})");
    smatch http_matches;

    if (!ec && bytes_transferred > 0) {
        // Extract received message and free the buffer
        string rx_message(boost::asio::buffer_cast<const char*>(m_recvBuffer.data()), bytes_transferred);
        m_recvBuffer.consume(bytes_transferred);
        m_message.append(rx_message);

        string line;
        string linedelimiter;
        size_t linedelimiteroffset;

        if (m_message.size() < 4)
            return; // Wait for other data to come in

        if (regex_search(m_message, http_matches, http_pattern, regex_constants::match_default)) {
            // We got an HTTP request
            string http_method = http_matches[1].str();
            string http_path = http_matches[2].str();
            string http_ver = http_matches[3].str();

            // Do we support method ?
            if (http_method != "GET") {
                string what = "Method " + http_method + " not allowed";
                stringstream ss;
                ss << http_ver << " "
                   << "405 Method not allowed\r\n"
                   << "Server: " << nsfminer_get_buildinfo()->project_name_with_version << "\r\n"
                   << "Content-Type: text/plain\r\n"
                   << "Content-Length: " << what.size() << "\r\n\r\n"
                   << what;
                sendSocketData(ss.str(), true);
                m_message.clear();
                cnote << "HTTP Request " << http_method << " " << http_path << " not supported (405).";
                return;
            }

            // Do we support path ?
            if (http_path != "/" && http_path != "/getstat1" && http_path != "/metrics") {
                string what = "The requested resource " + http_path + " not found on this server";
                stringstream ss;
                ss << http_ver << " "
                   << "404 Not Found\r\n"
                   << "Server: " << nsfminer_get_buildinfo()->project_name_with_version << "\r\n"
                   << "Content-Type: text/plain\r\n"
                   << "Content-Length: " << what.size() << "\r\n\r\n"
                   << what;
                sendSocketData(ss.str(), true);
                m_message.clear();
                cnote << "HTTP Request " << http_method << " " << http_path << " not found (404).";
                return;
            }

            //// Get all the lines - we actually don't care much
            //// until we support other http methods or paths
            //// Keep this for future use (if any)
            //// Remember to #include <boost/algorithm/string.hpp>
            // vector<string> lines;
            // boost::split(lines, m_message, [](char _c) { return _c == '\n'; });

            stringstream ss; // Builder of the response

            if (http_method == "GET" && (http_path == "/" || http_path == "/getstat1" || http_path == "/metrics")) {
                try {
                    string body, content_type;
                    if (http_path == "/metrics") {
                        body = getHttpMinerMetrics();
                        content_type = "text/plain";
                    } else {
                        body = getHttpMinerStatDetail();
                        content_type = "text/html";
                    }
                    ss.clear();
                    ss << http_ver << " "
                       << "200 OK\r\n"
                       << "Server: " << nsfminer_get_buildinfo()->project_name_with_version << "\r\n"
                       << "Content-Type: " << content_type << "; charset=utf-8\r\n"
                       << "Content-Length: " << body.size() << "\r\n\r\n"
                       << body;
                    cnote << "HTTP Request " << http_method << " " << http_path << " 200 OK (" << ss.str().size() << " bytes).";
                } catch (const exception& _ex) {
                    string what = "Internal error : " + string(_ex.what());
                    ss.clear();
                    ss << http_ver << " "
                       << "500 Internal Server Error\r\n"
                       << "Server: " << nsfminer_get_buildinfo()->project_name_with_version << "\r\n"
                       << "Content-Type: text/plain\r\n"
                       << "Content-Length: " << what.size() << "\r\n\r\n"
                       << what;
                    cnote << "HTTP Request " << http_method << " " << http_path << " 500 Error (" << _ex.what() << ").";
                }
            }

            sendSocketData(ss.str(), true);
            m_message.clear();
        } else {
            // We got a Json request
            // Process each line in the transmission
            linedelimiter = "\n";

            linedelimiteroffset = m_message.find(linedelimiter);
            while (linedelimiteroffset != string::npos) {
                if (linedelimiteroffset > 0) {
                    line = m_message.substr(0, linedelimiteroffset);
                    boost::trim(line);

                    if (!line.empty()) {
                        // Test validity of chunk and process
                        Json::Value jMsg;
                        Json::Value jRes;
                        Json::Reader jRdr;
                        if (jRdr.parse(line, jMsg)) {
                            try {
                                // Run in sync so no 2 different async reads may overlap
                                processRequest(jMsg, jRes);
                            } catch (const exception& _ex) {
                                jRes = Json::Value();
                                jRes["jsonrpc"] = "2.0";
                                jRes["id"] = Json::Value::null;
                                jRes["error"]["errorcode"] = "500";
                                jRes["error"]["message"] = _ex.what();
                            }
                        } else {
                            jRes = Json::Value();
                            jRes["jsonrpc"] = "2.0";
                            jRes["id"] = Json::Value::null;
                            jRes["error"]["errorcode"] = "-32700";
                            string what = jRdr.getFormattedErrorMessages();
                            boost::replace_all(what, "\n", " ");
                            cwarn << "API : Got invalid Json message " << what;
                            jRes["error"]["message"] = "Json parse error : " + what;
                        }

                        // Send response to client
                        sendSocketData(jRes);
                    }
                }

                // Next line (if any)
                m_message.erase(0, linedelimiteroffset + 1);
                linedelimiteroffset = m_message.find(linedelimiter);
            }

            // Eventually keep reading from socket
            if (m_socket.is_open())
                recvSocketData();
        }
    } else {
        disconnect();
    }
}

void ApiConnection::sendSocketData(Json::Value const& jReq, bool _disconnect) {
    if (!m_socket.is_open())
        return;
    stringstream line;
    line << Json::writeString(m_jSwBuilder, jReq) << endl;
    sendSocketData(line.str(), _disconnect);
}

void ApiConnection::sendSocketData(string const& _s, bool _disconnect) {
    if (!m_socket.is_open())
        return;
    ostream os(&m_sendBuffer);
    os << _s;

    async_write(m_socket, m_sendBuffer,
                m_io_strand.wrap(boost::bind(&ApiConnection::onSendSocketDataCompleted, this,
                                             boost::asio::placeholders::error, _disconnect)));
}

void ApiConnection::onSendSocketDataCompleted(const boost::system::error_code& ec, bool _disconnect) {
    if (ec || _disconnect)
        disconnect();
}

Json::Value ApiConnection::getMinerStat1() {
    auto connection = PoolManager::p().getActiveConnection();
    TelemetryType t = Farm::f().Telemetry();
    auto runningTime = chrono::duration_cast<chrono::minutes>(steady_clock::now() - t.start);

    ostringstream totalMhEth;
    ostringstream totalMhDcr;
    ostringstream detailedMhEth;
    ostringstream detailedMhDcr;
    ostringstream tempAndFans;
    ostringstream memTemps;
    ostringstream poolAddresses;
    ostringstream invalidStats;

    totalMhEth << fixed << setprecision(0) << t.farm.hashrate / 1000.0f << ";" << t.farm.solutions.accepted << ";"
               << t.farm.solutions.rejected;
    totalMhDcr << "0;0;0";                           // DualMining not supported
    invalidStats << t.farm.solutions.failed << ";0"; // Invalid + Pool switches
    poolAddresses << connection->Host() << ':' << connection->Port();
    invalidStats << ";0;0"; // DualMining not supported

    int gpuIndex;
    int numGpus = t.miners.size();

    for (gpuIndex = 0; gpuIndex < numGpus; gpuIndex++) {
        detailedMhEth << fixed << setprecision(0) << t.miners.at(gpuIndex).hashrate / 1000.0f
                      << (((numGpus - 1) > gpuIndex) ? ";" : "");
        detailedMhDcr << "off" << (((numGpus - 1) > gpuIndex) ? ";" : ""); // DualMining not supported
        tempAndFans << t.miners.at(gpuIndex).sensors.tempC << ";" << t.miners.at(gpuIndex).sensors.fanP
                    << (((numGpus - 1) > gpuIndex) ? ";" : ""); // Fetching Temp and Fans
        memTemps << t.miners.at(gpuIndex).sensors.memtempC
                 << (((numGpus - 1) > gpuIndex) ? ";" : ""); // Fetching Temp and Fans
    }

    Json::Value jRes;

    jRes[0] = nsfminer_get_buildinfo()->project_name_with_version; // miner version.
    jRes[1] = toString(runningTime.count());                       // running time, in minutes.
    jRes[2] = totalMhEth.str();    // total ETH hashrate in MH/s, number of ETH shares, number of ETH
                                   // rejected shares.
    jRes[3] = detailedMhEth.str(); // detailed ETH hashrate for all GPUs.
    jRes[4] = totalMhDcr.str();    // total DCR hashrate in MH/s, number of DCR shares, number of DCR
                                   // rejected shares.
    jRes[5] = detailedMhDcr.str(); // detailed DCR hashrate for all GPUs.
    jRes[6] = tempAndFans.str();   // Temperature and Fan speed(%) pairs for all GPUs.
    jRes[7] = poolAddresses.str(); // current mining pool. For dual mode, there will be two pools here.
    jRes[8] = invalidStats.str();  // number of ETH invalid shares, number of ETH pool switches,
                                   // number of DCR invalid shares, number of DCR pool switches.
    jRes[9] = memTemps.str();      // Memory temps

    return jRes;
}

Json::Value ApiConnection::getMinerStatDetailPerMiner(const TelemetryType& _t, shared_ptr<Miner> _miner) {
    unsigned _index = _miner->Index();
    chrono::steady_clock::time_point _now = chrono::steady_clock::now();

    Json::Value jRes;
    DeviceDescriptor minerDescriptor = _miner->getDescriptor();

    jRes["_index"] = _index;
    jRes["_mode"] = (minerDescriptor.subscriptionType == DeviceSubscriptionTypeEnum::Cuda ? "CUDA" : "OpenCL");

    /* Hardware Info */
    Json::Value hwinfo;
    if (minerDescriptor.uniqueId.substr(0, 5) == "0000:")
        hwinfo["pci"] = minerDescriptor.uniqueId.substr(5);
    else
        hwinfo["pci"] = minerDescriptor.uniqueId;
    hwinfo["type"] = (minerDescriptor.type == DeviceTypeEnum::Gpu
                          ? "GPU"
                          : (minerDescriptor.type == DeviceTypeEnum::Accelerator ? "ACCELERATOR" : "CPU"));
    ostringstream ss;
    ss << minerDescriptor.boardName << " " << dev::getFormattedMemory((double)minerDescriptor.totalMemory);
    hwinfo["name"] = ss.str();

    /* Hardware Sensors*/
    Json::Value sensors = Json::Value(Json::arrayValue);

    sensors.append(_t.miners.at(_index).sensors.tempC);
    sensors.append(_t.miners.at(_index).sensors.fanP);
    sensors.append(_t.miners.at(_index).sensors.powerW);
    sensors.append(_t.miners.at(_index).sensors.memtempC);

    hwinfo["sensors"] = sensors;

    /* Mining Info */
    Json::Value mininginfo;
    Json::Value jshares = Json::Value(Json::arrayValue);
    Json::Value jsegment = Json::Value(Json::arrayValue);
    jshares.append(_t.miners.at(_index).solutions.accepted);
    jshares.append(_t.miners.at(_index).solutions.rejected);
    jshares.append(_t.miners.at(_index).solutions.failed);

    auto solution_lastupdated = chrono::duration_cast<chrono::seconds>(_now - _t.miners.at(_index).solutions.tstamp);
    jshares.append(uint64_t(solution_lastupdated.count())); // interval in seconds from last found
                                                            // share

    mininginfo["shares"] = jshares;
    mininginfo["paused"] = _miner->paused();
    mininginfo["pause_reason"] = _miner->paused() ? _miner->pausedString() : Json::Value::null;

    /* Hash & Share infos */
    mininginfo["hashrate"] = toHex((uint32_t)_t.miners.at(_index).hashrate, HexPrefix::Add);

    jRes["hardware"] = hwinfo;
    jRes["mining"] = mininginfo;

    return jRes;
}

string ApiConnection::getHttpMinerMetrics() {
    Json::Value jStat = getMinerStatDetail();
    Json::StreamWriterBuilder builder;

    ostringstream ss;
    ss << "host=" << jStat["host"]["name"] << ",version=" << jStat["host"]["version"];
    string labels = ss.str();
    stringstream _ret;
    _ret
        << "# HELP miner_process_runtime Number of seconds miner process has been running.\n"
        << "# TYPE miner_process_runtime gauge\n"
	    << "miner_process_runtime{" << labels << "} " << jStat["host"]["runtime"] << "\n"
        << "# HELP miner_process_connected Connection status.\n"
        << "# TYPE miner_process_connected gauge\n"
	    << "miner_process_connected{" << labels << ",uri=" << jStat["connection"]["uri"] << "} " << jStat["connection"]["connected"].asUInt() << "\n"
        << "# HELP miner_process_connection_switches Connection switches.\n"
        << "# TYPE miner_process_connection_switches gauge\n"
	    << "miner_process_connection_switches{" << labels << "} " << jStat["connection"]["switches"] << "\n";

    // Per device help/type info.

    double total_power = 0;
    for (Json::Value::ArrayIndex i = 0; i != jStat["devices"].size(); i++) {
        Json::Value device = jStat["devices"][i];
        ostringstream ss;
        ss << labels
           << ",id=\"" << device["_index"] << "\""
           << ",name=" << device["hardware"]["name"]
           << ",pci=" << device["hardware"]["pci"]
           << ",device_type=" << device["hardware"]["type"]
           << ",mode=" << device["_mode"];
        string device_labels = ss.str();

        double hashrate = stoul(device["mining"]["hashrate"].asString(), nullptr, 16);
        double power = device["hardware"]["sensors"][2].asDouble();

        _ret
            << "# HELP miner_device_hashrate Device hash rate in hashes/sec.\n"
            << "# TYPE miner_device_hashrate gauge\n"
            << "miner_device_hashrate{" << device_labels << "} " << hashrate << "\n"
            << "# HELP miner_device_temp_celsius Device temperature in degrees celsius.\n"
            << "# TYPE miner_device_temp_celsius gauge\n"
            << "miner_device_temp_celsius{" << device_labels << "} " << device["hardware"]["sensors"][0].asDouble() << "\n"
            << "# HELP miner_device_memory_temp_celsius Memory temperature in degrees celsius.\n"
            << "# TYPE miner_device_memory_temp_celsius gauge\n"
            << "miner_device_memory_temp_celsius{" << device_labels << "} " << device["hardware"]["sensors"][3].asDouble() << "\n"
            << "# HELP miner_device_fanspeed Device fanspeed (percentage 0-100).\n"
            << "# TYPE miner_device_fanspeed gauge\n"
            << "miner_device_fanspeed{" << device_labels << "} " << device["hardware"]["sensors"][1].asUInt() << "\n"
            << "# HELP miner_device_fanspeed Device fanspeed (percentage 0-100).\n"
            << "# TYPE miner_device_fanspeed gauge\n"
            << "miner_device_fanspeed{" << device_labels << "} " << device["hardware"]["sensors"][1].asUInt() << "\n"
            << "# HELP miner_device_shares_total Number of shares processed by devices and status (failed, found, or rejected).\n"
            << "# TYPE miner_device_shares_total counter\n"
            << "miner_device_shares_total{" << device_labels << ",status=\"found\"} " << device["mining"]["shares"][0].asUInt() << "\n"
            << "miner_device_shares_total{" << device_labels << ",status=\"rejected\"} " << device["mining"]["shares"][1].asUInt() << "\n"
            << "miner_device_shares_total{" << device_labels << ",status=\"failed\"} " << device["mining"]["shares"][2].asUInt() << "\n"
            << "# HELP miner_device_shares_last_found_seconds Time since device last found share (seconds).\n"
            << "# TYPE miner_device_shares_last_found_seconds gauge\n"
            << "miner_device_shares_last_found_seconds{" << device_labels << "} " << device["mining"]["shares"][3].asUInt() << "\n"
            << "# HELP miner_device_paused True if device is paused.\n"
            << "# TYPE miner_device_paused gauge\n"
            << "miner_device_paused{" << device_labels << "} " << (device["mining"]["paused"].asBool() ? 1 : 0) << "\n";

        total_power += power;
    }
    double total_hashrate = stoul(jStat["mining"]["hashrate"].asString(), nullptr, 16);
    _ret << "# HELP miner_total_hashrate Total miner process hashrate across all devices (hashes/sec).\n"
         << "# TYPE miner_total_hashrate gauge\n"
         << "miner_total_hashrate{" << labels << "} " << total_hashrate << "\n"
         << "# HELP miner_total_power Total power consumption across all devices (watts).\n"
         << "# TYPE miner_total_power gauge\n"
         << "miner_total_power{" << labels << "} " << total_power << "\n"
         << "# HELP miner_shares_total Total number of shares across all devices.\n"
         << "# TYPE miner_shares_total counter\n"
         << "miner_shares_total{" << labels << ",status=\"found\"} " << jStat["mining"]["shares"][0].asUInt() << "\n"
         << "miner_shares_total{" << labels << ",status=\"rejected\"} " << jStat["mining"]["shares"][1].asUInt() << "\n"
         << "miner_shares_total{" << labels << ",status=\"failed\"} " << jStat["mining"]["shares"][2].asUInt() << "\n"
         << "# HELP miner_difficulty Difficulty mining.\n"
         << "# TYPE miner_difficulty gauge\n"
         << "miner_difficulty{" << labels << "} " << jStat["mining"]["difficulty"].asDouble() << "\n"
         << "# HELP miner_shares_last_found_seconds Time since last found share across all devices (seconds).\n"
         << "# TYPE miner_shares_last_found_seconds gauge\n"
         << "miner_shares_last_found_secs{" << labels << "} " << jStat["mining"]["shares"][3].asUInt() << "\n";

    // For debugging
    //_ret << Json::writeString(builder, jStat);

    _ret << "# EOF\n";
    return _ret.str();
}

string ApiConnection::getHttpMinerStatDetail() {
    Json::Value jStat = getMinerStatDetail();
    uint64_t durationSeconds = jStat["host"]["runtime"].asUInt64();
    int hours = (int)(durationSeconds / 3600);
    durationSeconds -= (hours * 3600);
    int minutes = (int)(durationSeconds / 60);
    int hoursSize = (hours > 9 ? (hours > 99 ? 3 : 2) : 1);

    /* Build up header*/
    stringstream _ret;
    _ret << "<!doctype html>"
         << "<html lang=en>"
         << "<head>"
         << "<meta charset=utf-8>"
         << "<meta http-equiv=\"refresh\" content=\"30\">"
         << "<title>" << jStat["host"]["name"].asString() << "</title>"
         << "<style>"
         << "body{font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",Roboto,"
         << "\"Helvetica Neue\",Helvetica,Arial,sans-serif;font-size:16px;line-height:1.5;"
         << "text-align:center;}"
         << "table,td,th{border:1px inset #000;}"
         << "table{border-spacing:0;}"
         << "td,th{padding:3px;}"
         << "tbody tr:nth-child(even){background-color:" << HTTP_ROW0_COLOR << ";}"
         << "tbody tr:nth-child(odd){background-color:" << HTTP_ROW1_COLOR << ";}"
         << ".mx-auto{margin-left:auto;margin-right:auto;}"
         << ".bg-header1{background-color:" << HTTP_HDR1_COLOR << ";}"
         << ".bg-header0{background-color:" << HTTP_HDR0_COLOR << ";}"
         << ".bg-red{color:" << HTTP_ROWRED_COLOR << ";}"
         << ".right{text-align: right;}"
         << "</style>"
         << "<meta http-equiv=refresh content=30>"
         << "</head>"
         << "<body>"
         << "<table class=mx-auto>"
         << "<thead>"
         << "<tr class=bg-header1>"
         << "<th colspan=9>" << jStat["host"]["version"].asString() << " - " << setw(hoursSize) << hours << ":"
         << setw(2) << setfill('0') << fixed << minutes << "<br>Pool: " << jStat["connection"]["uri"].asString()
         << "</th>"
         << "</tr>"
         << "<tr class=bg-header0>"
         << "<th>PCI</th>"
         << "<th>Device</th>"
         << "<th>Mode</th>"
         << "<th>Paused</th>"
         << "<th class=right>Hash Rate</th>"
         << "<th class=right>Solutions</th>"
         << "<th class=right>Temp.</th>"
         << "<th class=right>Fan %</th>"
         << "<th class=right>Power</th>"
         << "</tr>"
         << "</thead><tbody>";

    /* Loop miners */
    double total_hashrate = 0;
    double total_power = 0;
    unsigned int total_solutions = 0;

    for (Json::Value::ArrayIndex i = 0; i != jStat["devices"].size(); i++) {
        Json::Value device = jStat["devices"][i];
        double hashrate = stoul(device["mining"]["hashrate"].asString(), nullptr, 16);
        double power = device["hardware"]["sensors"][2].asDouble();
        unsigned int solutions = device["mining"]["shares"][0].asUInt();
        total_hashrate += hashrate;
        total_power += power;
        total_solutions += solutions;

        _ret << "<tr" << (device["mining"]["paused"].asBool() ? " class=\"bg-red\"" : "") << ">"; // Open row

        _ret << "<td>" << device["hardware"]["pci"].asString() << "</td>";
        _ret << "<td>" << device["hardware"]["name"].asString() << "</td>";
        _ret << "<td>" << device["_mode"].asString() << "</td>";

        _ret << "<td>" << (device["mining"]["paused"].asBool() ? device["mining"]["pause_reason"].asString() : "No")
             << "</td>";

        _ret << "<td class=right>" << dev::getFormattedHashes(hashrate) << "</td>";

        string solString = "A" + device["mining"]["shares"][0].asString() + ":R" +
                           device["mining"]["shares"][1].asString() + ":F" + device["mining"]["shares"][2].asString();
        _ret << "<td class=right>" << solString << "</td>";
        _ret << "<td class=right>" << device["hardware"]["sensors"][0].asString() << "</td>";
        _ret << "<td class=right>" << device["hardware"]["sensors"][1].asString() << "</td>";

        stringstream powerStream; // Round the power to 2 decimal places to remove floating point
                                  // garbage
        powerStream << fixed << setprecision(2) << device["hardware"]["sensors"][2].asDouble();
        _ret << "<td class=right>" << powerStream.str() << "</td>";

        _ret << "</tr>"; // Close row
    }
    _ret << "</tbody>";

    /* Summarize */
    _ret << "<tfoot><tr class=bg-header0><td colspan=4 class=right>Total</td><td class=right>"
         << dev::getFormattedHashes(total_hashrate) << "</td><td class=right>" << total_solutions
         << "</td><td colspan=3 class=right>" << setprecision(2) << total_power << "</td></tfoot>";

    _ret << "</table></body></html>";
    return _ret.str();
}

Json::Value ApiConnection::getMinerStatDetail() {
    const chrono::steady_clock::time_point now = chrono::steady_clock::now();
    TelemetryType t = Farm::f().Telemetry();

    auto runningTime = chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - t.start);

    // ostringstream version;
    Json::Value devices = Json::Value(Json::arrayValue);
    Json::Value jRes;

    /* Host Info */
    Json::Value hostinfo;
    hostinfo["version"] = nsfminer_get_buildinfo()->project_name_with_version; // miner version.
    hostinfo["runtime"] = uint64_t(runningTime.count());                       // running time, in seconds.

    {
        // Even the client should know which host was queried
        char hostName[HOST_NAME_MAX + 1];
        if (!gethostname(hostName, HOST_NAME_MAX + 1))
            hostinfo["name"] = hostName;
        else
            hostinfo["name"] = Json::Value::null;
    }

    /* Connection info */
    Json::Value connectioninfo;
    auto connection = PoolManager::p().getActiveConnection();
    connectioninfo["uri"] = connection->str();
    connectioninfo["connected"] = PoolManager::p().isConnected();
    connectioninfo["switches"] = PoolManager::p().getConnectionSwitches();

    /* Mining Info */
    Json::Value mininginfo;
    Json::Value sharesinfo = Json::Value(Json::arrayValue);

    mininginfo["hashrate"] = toHex(uint32_t(t.farm.hashrate), HexPrefix::Add);
    mininginfo["epoch"] = PoolManager::p().getCurrentEpoch();
    mininginfo["epoch_changes"] = PoolManager::p().getEpochChanges();
    mininginfo["difficulty"] = PoolManager::p().getPoolDifficulty();

    sharesinfo.append(t.farm.solutions.accepted);
    sharesinfo.append(t.farm.solutions.rejected);
    sharesinfo.append(t.farm.solutions.failed);
    auto solution_lastupdated = chrono::duration_cast<chrono::seconds>(now - t.farm.solutions.tstamp);
    sharesinfo.append(uint64_t(solution_lastupdated.count())); // interval in seconds from last
                                                               // found share
    mininginfo["shares"] = sharesinfo;

    /* Monitors Info */
    Json::Value monitorinfo;
    auto tstop = Farm::f().get_tstop();
    if (tstop) {
        Json::Value tempsinfo = Json::Value(Json::arrayValue);
        tempsinfo.append(Farm::f().get_tstart());
        tempsinfo.append(tstop);
        monitorinfo["temperatures"] = tempsinfo;
    }

    /* Devices related info */
    for (shared_ptr<Miner> miner : Farm::f().getMiners())
        devices.append(getMinerStatDetailPerMiner(t, miner));

    jRes["devices"] = devices;

    jRes["monitors"] = monitorinfo;
    jRes["connection"] = connectioninfo;
    jRes["host"] = hostinfo;
    jRes["mining"] = mininginfo;

    return jRes;
}
