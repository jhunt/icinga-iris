IRIS
====

A high-performance, passive check accepting  event broker for Icinga/Nagios

Works with Icinga 1.x

PATCHES
=======

In order to use this, you will need the following three patches:

- Registered File Descriptors Patch (https://dev.icinga.org/issues/8139)
- Check Result List Mutex Patch (https://dev.icinga.org/issues/8140)

You may also want this patch, although it is not required for the event
broker:

- Optimized Freshness Checking Patch (https://dev.icinga.org/issues/8141)

(It *is* however useful for running a passive-heavy monitoring system)

COPYRIGHT AND LICENCE
=====================

Copyright (C) 2014 Synacor, Inc.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
