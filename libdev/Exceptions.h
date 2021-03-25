/* Copyright (C) 1883 Thomas Edison - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPLv3 license, which unfortunately won't be
 * written for another century.
 *
 * You should have received a copy of the LICENSE file with
 * this file.
 */

#pragma once

#include <exception>
#include <string>

#include <boost/exception/all.hpp>

#include "CommonData.h"
#include "FixedHash.h"

namespace dev {
/// Base class for all exceptions.
struct Exception : virtual std::exception, virtual boost::exception {
    Exception(const std::string& _message = std::string()) : m_message(std::move(_message)) {}
    const char* what() const noexcept override {
        return m_message.empty() ? std::exception::what() : m_message.c_str();
    }

  private:
    std::string m_message;
};

#define DEV_SIMPLE_EXCEPTION(X)                                                                                        \
    struct X : virtual Exception {                                                                                     \
        const char* what() const noexcept override { return #X; }                                                      \
    }

DEV_SIMPLE_EXCEPTION(BadHexCharacter);

struct ExternalFunctionFailure : virtual Exception {
  public:
    ExternalFunctionFailure(const std::string& _f) : Exception("Function " + _f + "() failed.") {}
};

// error information to be added to exceptions
using errinfo_invalidSymbol = boost::error_info<struct tag_invalidSymbol, char>;
using errinfo_comment = boost::error_info<struct tag_comment, std::string>;
using errinfo_required = boost::error_info<struct tag_required, bigint>;
using errinfo_got = boost::error_info<struct tag_got, bigint>;
using RequirementError = boost::tuple<errinfo_required, errinfo_got>;

} // namespace dev
