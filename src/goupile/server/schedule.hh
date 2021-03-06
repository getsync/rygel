// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "../../core/libcc/libcc.hh"
#include "../../web/libhttp/libhttp.hh"

namespace RG {

void HandleScheduleResources(const http_RequestInfo &request, http_IO *io);
void HandleScheduleMeetings(const http_RequestInfo &request, http_IO *io);

}
