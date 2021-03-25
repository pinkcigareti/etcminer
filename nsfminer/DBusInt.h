/* Copyright (C) 1883 Thomas Edison - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the GPLv3 license, which unfortunately won't be
 * written for another century.
 *
 * You should have received a copy of the LICENSE file with
 * this file.
 */

#pragma once

#include <dbus/dbus.h>

using namespace std;

class DBusInt {
  public:
    DBusInt() {
        dbus_error_init(&err);
        conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
        if (!conn)
            ccrit << "DBus error " << err.name << ": " << err.message;
        dbus_bus_request_name(conn, "eth.miner", DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
        if (dbus_error_is_set(&err)) {
            cnote << "DBus error " << err.name << ": " << err.message;
            dbus_connection_close(conn);
        }
        cnote << "DBus initialized!";
    }

    void send(const char* hash) {
        DBusMessage* msg;
        msg = dbus_message_new_signal("/eth/miner/hash", "eth.miner.monitor", "Hash");
        if (msg == nullptr)
            ccrit << "Message is null!";
        dbus_message_append_args(msg, DBUS_TYPE_STRING, &hash, DBUS_TYPE_INVALID);
        if (!dbus_connection_send(conn, msg, nullptr))
            ccrit << "Error sending message!";
        dbus_message_unref(msg);
    }

  private:
    DBusError err;
    DBusConnection* conn;
};
