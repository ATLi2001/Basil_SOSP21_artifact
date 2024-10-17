/***********************************************************************
 *
 * Copyright 2024 Austin Li <atl63@cornell.edu>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************************/

#include "store/sintrstore/validation/validation_client.h"

namespace sintrstore {

ValidationClient::ValidationClient() {
}

ValidationClient::~ValidationClient() {
}

// Begin a transaction.
void ValidationClient::Begin(begin_callback bcb, begin_timeout_callback btcb,
    uint32_t timeout, bool retry, const std::string &txnState) {};

// Get the value corresponding to key.
void ValidationClient::Get(const std::string &key, get_callback gcb,
    get_timeout_callback gtcb, uint32_t timeout) {};

// Set the value for the given key.
void ValidationClient::Put(const std::string &key, const std::string &value,
    put_callback pcb, put_timeout_callback ptcb,
    uint32_t timeout) {};

// Commit all Get(s) and Put(s) since Begin().
void ValidationClient::Commit(commit_callback ccb, commit_timeout_callback ctcb,
    uint32_t timeout) {};

// Abort all Get(s) and Put(s) since Begin().
void ValidationClient::Abort(abort_callback acb, abort_timeout_callback atcb,
    uint32_t timeout) {};

} // namespace sintrstore
