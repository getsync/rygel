// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#pragma once

#include "../../core/libcc/libcc.hh"

namespace RG {

bool InitFiles();

void HandleFileList(const http_RequestInfo &request, http_IO *io);
bool HandleFileGet(const http_RequestInfo &request, http_IO *io);
void HandleFilePut(const http_RequestInfo &request, http_IO *io);
void HandleFileDelete(const http_RequestInfo &request, http_IO *io);

}
