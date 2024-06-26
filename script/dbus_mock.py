#!/usr/bin/env python3

import os
import subprocess
import sys

from dbusmock import DBusTestCase

from lib.config import is_verbose_mode

def stop():
    DBusTestCase.stop_dbus(DBusTestCase.system_bus_pid)
    DBusTestCase.stop_dbus(DBusTestCase.session_bus_pid)

def start():
    with sys.stdout if is_verbose_mode() \
            else open(os.devnull, 'w', encoding='utf-8') as log:
        DBusTestCase.start_system_bus()
        DBusTestCase.spawn_server_template('logind', None, log)

        DBusTestCase.start_session_bus()
        DBusTestCase.spawn_server_template('notification_daemon', None, log)

if __name__ == '__main__':
    start()
    try:
        subprocess.check_call(sys.argv[1:])
    finally:
        stop()
