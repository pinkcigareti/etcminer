
/* Copyright (C) 1883 Thomas Edison - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPLv3 license, which unfortunately won't be
 * written for another century.
 *
 * You should have received a copy of the LICENSE file with
 * this file.
 */

#include <algorithm>
#include <iostream>
#include <map>
#include <sstream>

#include <cstring>

#include <libpool/PoolURI.h>

using namespace dev;
using namespace std;

struct SchemeAttributes {
    ProtocolFamily family;
    SecureLevel secure;
    unsigned version;
};

static map<string, SchemeAttributes> s_schemes = {
    /*
    This schemes are kept for backwards compatibility.
    Ethminer do perform stratum autodetection
    */
    {"stratum+tcp", {ProtocolFamily::STRATUM, SecureLevel::NONE, 0}},
    {"stratum1+tcp", {ProtocolFamily::STRATUM, SecureLevel::NONE, 1}},
    {"stratum2+tcp", {ProtocolFamily::STRATUM, SecureLevel::NONE, 2}},
    {"stratum3+tcp", {ProtocolFamily::STRATUM, SecureLevel::NONE, 3}},
    {"stratum+ssl", {ProtocolFamily::STRATUM, SecureLevel::TLS, 0}},
    {"stratum1+ssl", {ProtocolFamily::STRATUM, SecureLevel::TLS, 1}},
    {"stratum2+ssl", {ProtocolFamily::STRATUM, SecureLevel::TLS, 2}},
    {"stratum3+ssl", {ProtocolFamily::STRATUM, SecureLevel::TLS, 3}},
    {"http", {ProtocolFamily::GETWORK, SecureLevel::NONE, 0}},
    {"getwork", {ProtocolFamily::GETWORK, SecureLevel::NONE, 0}},

    /*
    Any TCP scheme has, at the moment, only STRATUM protocol thus
    reiterating "stratum" word would be pleonastic
    Version 9 means auto-detect stratum mode
    */

    {"stratum", {ProtocolFamily::STRATUM, SecureLevel::NONE, 999}},
    {"stratums", {ProtocolFamily::STRATUM, SecureLevel::TLS, 999}},

    /*
    The following scheme is only meant for simulation operations
    It's not meant to be used with -P arguments
    */

    {"simulation", {ProtocolFamily::SIMULATION, SecureLevel::NONE, 999}}};

static bool url_decode(const string& in, string& out) {
    out.clear();
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%') {
            if (i + 3 <= in.size()) {
                int value = 0;
                istringstream is(in.substr(i + 1, 2));
                if (is >> hex >> value) {
                    out += static_cast<char>(value);
                    i += 2;
                } else {
                    return false;
                }
            } else {
                return false;
            }
        } else if (in[i] == '+') {
            out += ' ';
        } else {
            out += in[i];
        }
    }
    return true;
}

/*
  For a well designed explanation of URI parts
  refer to https://cpp-netlib.org/0.10.1/in_depth/uri.html
*/

URI::URI(string uri, bool _sim) : m_uri{move(uri)} {
    regex sch_auth("^([a-zA-Z0-9\\+]{1,})\\:\\/\\/(.*)$");
    smatch matches;
    if (!regex_search(m_uri, matches, sch_auth, regex_constants::match_default))
        return;

    // Split scheme and authoority
    // Authority MUST be valued
    m_scheme = matches[1].str();
    boost::algorithm::to_lower(m_scheme);
    m_authority = matches[2].str();

    // Missing authority is not possible
    if (m_authority.empty())
        throw runtime_error("Invalid authority");

    // Simulation scheme is only allowed if specifically set
    if (!_sim && m_scheme == "simulation")
        throw runtime_error("Invalid scheme");

    // Check scheme is allowed
    if ((s_schemes.find(m_scheme) == s_schemes.end()))
        throw runtime_error("Invalid scheme");

    // Now let's see if authority part can be split into userinfo and "the rest"
    regex usr_url("^(.*)\\@(.*)$");
    if (regex_search(m_authority, matches, usr_url, regex_constants::match_default)) {
        m_userinfo = matches[1].str();
        m_urlinfo = matches[2].str();
    } else
        m_urlinfo = m_authority;

    /*
      If m_userinfo present and valued it can be composed by either :
      - user
      - user.worker
      - user.worker:password
      - user:password

      In other words . delimits the beginning of worker and : delimits
      the beginning of password

    */
    if (!m_userinfo.empty()) {
        // Save all parts enclosed in backticks into a dictionary
        // and replace them with tokens in the authority
        regex btick("`((?:[^`])*)`");
        map<string, string> btick_blocks;
        auto btick_blocks_begin = sregex_iterator(m_authority.begin(), m_authority.end(), btick);
        auto btick_blocks_end = sregex_iterator();
        int i = 0;
        for (sregex_iterator it = btick_blocks_begin; it != btick_blocks_end; ++it) {
            smatch match = *it;
            string match_str = match[1].str();
            btick_blocks["_" + to_string(i++)] = match[1].str();
        }
        if (btick_blocks.size()) {
            map<string, string>::iterator it;
            for (it = btick_blocks.begin(); it != btick_blocks.end(); it++)
                boost::replace_all(m_userinfo, "`" + it->second + "`", "`" + it->first + "`");
        }

        vector<regex> usr_patterns;
        usr_patterns.push_back(regex("^(.*)\\.(.*)\\:(.*)$"));
        usr_patterns.push_back(regex("^(.*)\\:(.*)$"));
        usr_patterns.push_back(regex("^(.*)\\.(.*)$"));
        bool usrMatchFound = false;
        for (size_t i = 0; i < usr_patterns.size() && !usrMatchFound; i++) {
            if (regex_search(m_userinfo, matches, usr_patterns.at(i), regex_constants::match_default)) {
                usrMatchFound = true;
                switch (i) {
                case 0:
                    m_user = matches[1].str();
                    m_worker = matches[2].str();
                    m_password = matches[3].str();
                    break;
                case 1:
                    m_user = matches[1];
                    m_password = matches[2];
                    break;
                case 2:
                    m_user = matches[1];
                    m_worker = matches[2];
                    break;
                default:
                    break;
                }
            }
        }
        // If no matches found after this loop it means all the user
        // part is only user login
        if (!usrMatchFound)
            m_user = m_userinfo;

        // Replace all tokens with their respective values
        if (btick_blocks.size()) {
            map<string, string>::iterator it;
            for (it = btick_blocks.begin(); it != btick_blocks.end(); it++) {
                boost::replace_all(m_userinfo, "`" + it->first + "`", it->second);
                boost::replace_all(m_user, "`" + it->first + "`", it->second);
                boost::replace_all(m_worker, "`" + it->first + "`", it->second);
                boost::replace_all(m_password, "`" + it->first + "`", it->second);
            }
        }
    }

    /*
      Let's process the url part which must contain at least a host
      an optional port and eventually a path (which may include a query
      and a fragment)
      Host can be a DNS host or an IP address.
      Thus we can have
      - host
      - host/path
      - host:port
      - host:port/path
    */
    size_t offset = m_urlinfo.find('/');
    if (offset != string::npos) {
        m_hostinfo = m_urlinfo.substr(0, offset);
        m_pathinfo = m_urlinfo.substr(offset);
    } else {
        m_hostinfo = m_urlinfo;
    }
    boost::algorithm::to_lower(m_hostinfo); // needed to ensure we properly hit "exit" as host
    regex host_pattern("^(.*)\\:([0-9]{1,5})$");
    if (regex_search(m_hostinfo, matches, host_pattern, regex_constants::match_default)) {
        m_host = matches[1].str();
        m_port = boost::lexical_cast<uint16_t>(matches[2].str());
    } else {
        m_host = m_hostinfo;
    }

    // Host info must be present and valued
    if (m_host.empty())
        throw runtime_error("Missing host");

    /*
      Eventually split path info into path query fragment
    */
    if (!m_pathinfo.empty()) {
        // Url Decode Path

        vector<regex> path_patterns;
        path_patterns.push_back(regex("(\\/.*)\\?(.*)\\#(.*)$"));
        path_patterns.push_back(regex("(\\/.*)\\#(.*)$"));
        path_patterns.push_back(regex("(\\/.*)\\?(.*)$"));
        bool pathMatchFound = false;
        for (size_t i = 0; i < path_patterns.size() && !pathMatchFound; i++) {
            if (regex_search(m_pathinfo, matches, path_patterns.at(i), regex_constants::match_default)) {
                pathMatchFound = true;
                switch (i) {
                case 0:
                    m_path = matches[1].str();
                    m_query = matches[2].str();
                    m_fragment = matches[3].str();
                    break;
                case 1:
                    m_path = matches[1].str();
                    m_fragment = matches[2].str();
                    break;
                case 2:
                    m_path = matches[1].str();
                    m_query = matches[2].str();
                    break;
                default:
                    break;
                }
            }
            // If no matches found after this loop it means all the pathinfo
            // part is only path login
            if (!pathMatchFound)
                m_path = m_pathinfo;
        }
    }

    // Determine host type
    boost::system::error_code ec;
    boost::asio::ip::address address = boost::asio::ip::address::from_string(m_host, ec);
    if (!ec) {
        // This is a valid Ip Address
        if (address.is_v4())
            m_hostType = UriHostNameType::IPV4;
        if (address.is_v6())
            m_hostType = UriHostNameType::IPV6;

        m_isLoopBack = address.is_loopback();
    } else {
        // Check if valid DNS hostname
        regex hostNamePattern("^(([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\\-]*[a-zA-Z0-9])\\.)*([A-Za-z0-9]|[A-Za-z0-9][A-"
                              "Za-z0-9\\-]*[A-Za-z0-9])$");
        if (regex_match(m_host, hostNamePattern))
            m_hostType = UriHostNameType::Dns;
        else
            m_hostType = UriHostNameType::Basic;
    }

    if (!m_user.empty())
        boost::replace_all(m_user, "`", "");
    if (!m_password.empty())
        boost::replace_all(m_password, "`", "");
    if (!m_worker.empty())
        boost::replace_all(m_worker, "`", "");

    // Eventually decode every encoded char
    string tmpStr;
    if (url_decode(m_userinfo, tmpStr))
        m_userinfo = tmpStr;
    if (url_decode(m_urlinfo, tmpStr))
        m_urlinfo = tmpStr;
    if (url_decode(m_hostinfo, tmpStr))
        m_hostinfo = tmpStr;
    if (url_decode(m_pathinfo, tmpStr))
        m_pathinfo = tmpStr;

    if (url_decode(m_path, tmpStr))
        m_path = tmpStr;
    if (url_decode(m_query, tmpStr))
        m_query = tmpStr;
    if (url_decode(m_fragment, tmpStr))
        m_fragment = tmpStr;
    if (url_decode(m_user, tmpStr))
        m_user = tmpStr;
    if (url_decode(m_password, tmpStr))
        m_password = tmpStr;
    if (url_decode(m_worker, tmpStr))
        m_worker = tmpStr;
}

ProtocolFamily URI::Family() const { return s_schemes[m_scheme].family; }

unsigned URI::Version() const { return s_schemes[m_scheme].version; }

string URI::UserDotWorker() const {
    string _ret = m_user;
    if (!m_worker.empty())
        _ret.append("." + m_worker);
    return _ret;
}

SecureLevel URI::SecLevel() const { return s_schemes[m_scheme].secure; }

UriHostNameType URI::HostNameType() const { return m_hostType; }

bool URI::IsLoopBack() const { return m_isLoopBack; }

string URI::KnownSchemes(ProtocolFamily family) {
    string schemes;
    for (const auto& s : s_schemes) {
        if ((s.second.family == family) && (s.second.version != 999))
            schemes += s.first + " ";
    }
    return schemes;
}
