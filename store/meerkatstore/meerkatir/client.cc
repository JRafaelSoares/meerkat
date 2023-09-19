// -*- mode: c++; c-file-style: "k&r"; c-basic-offset: 4 -*-
/***********************************************************************
 *
 * store/meerkatstore/meerkatir/client.cc:
 *   Meerkatir client interface (uses meerkatir for replcation and the
 *   meerkatstore transactional storage system).
 *
 * Copyright 2015 Irene Zhang <iyzhang@cs.washington.edu>
 *                Naveen Kr. Sharma <naveenks@cs.washington.edu>
 *           2018 Adriana Szekeres <aaasz@cs.washington.edu>
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

#include "store/common/consts.h"
#include "store/meerkatstore/meerkatir/client.h"
#include "network/manager.h"
#include "util/switch.h"

#include <chrono>
#include <cstring>
#include <random>
#include <list>
#include <limits.h>
#include <thread>

#include <iostream>

namespace meerkatstore {
namespace meerkatir {

using namespace std;

/** create a logger for this file */
static zip::util::logger logger("zipkat_client");

Client::Client(int nsthreads, int nShards, uint32_t id,
               std::shared_ptr<zip::client::client> client,
               zip::network::manager& manager, std::string order)
    : client_id(id), t_id(0),
      ziplogClient(client),
      ziplogBuffer(manager.get_buffers(zip::consts::PAGE_SIZE, /* num_buffers */1))
{
    // Have this assert as we'll only use the first element in the list later.
    Assert(ziplogBuffer.size() == 1);
    // Initialize all state here;
    srand(time(NULL));

    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis(1, ULLONG_MAX);

    auto queues = manager.create_recv_queues(2);
    recv_queue_ = std::move(queues.at(0));
    zipkat_get_recv_queue_ = std::move(queues.at(1));

    // Contact the order server to obtain all meerkat replicas

    // initialise the struct which is client's first message
    intro_.message_type = zip::api::ZIPKAT_CLIENT_INTRO;
    intro_.client_id = client_id;

    // connect with the ordering service
    auto [ip, port] = zip::util::split_address(order);
    auto [send_queue, buffer, length] = manager.connect(ip, port, &intro_, intro_.length());
    order_ = std::move(send_queue);

    while (!stop_setup_.load(std::memory_order_relaxed)) {
        zip::util::recv_apply(logger, recv_queue_,
                              [&] (zip::api::client_zipkat_storage_info& info) {
                                    // Replica id that the client will stick to
                                    sticky_replica_ = client_id % info.replication_factor;

                                    int current_shard = 0;
                                    int current_server = 0;
                                  // connect to all the servers in the list
                                  for (unsigned long i = 0; i < info.num_servers; i++) {
                                      // verify the server's shard ID
                                      auto [send_queues, buffer, length] = manager.connect(info.addresses[i], &intro_, intro_.length(), 0);

                                      // verify the server's intro message
                                      auto& intro = *static_cast<zip::api::zipkat_storage_intro*>(buffer.get());

                                      ZIP_ASSERT_EQ(length, intro.length(), "length of the received packet is invalid ", length, ", ", intro.length());
                                      ZIP_ASSERT_EQ(intro.message_type, zip::api::ZIPKAT_STORAGE_INTRO, "received an unknown type of message (", intro.message_type, ")");

                                      replica_queues_[current_shard].emplace_back(std::move(send_queues.front()));
                                      if (++current_server == info.replication_factor) {
                                          current_shard++;
                                      }
                                      // Picks the get queue the client will contact
                                      zipkat_get_dest_ = client_id % intro.get_queues;
                                  }
                                  stop_setup_.store(true, std::memory_order_relaxed);
                              }
        );
    }
/*
    while (client_id == 0) {
        client_id = dis(gen);
    }
*/

    // Standard mersenne_twister_engine seeded with rd()
    // core_gen = std::mt19937(rd());

    Warning("Initializing Meerkatstore client with id [%lu]", client_id);

    Debug("Meerkatstore client [%lu] created!", client_id);

#ifdef ZIP_MEASURE
    hdr_init(1, 10000, 3, &hist_get);
    hdr_init(1, 10000, 3, &hist_commit);
    hdr_init(1, 10000, 3, &hist_yield);
    hdr_count_get = 0;
    hdr_count_commit = 0;
    hdr_count_yield = 0;
#endif

    txn = Transaction();

    // TODO: remove
    //buf = (char*)malloc(1024);
}

/* Begins a transaction. All subsequent operations before a commit() or
 * abort() are part of this transaction.
 *
 * Return a TID for the transaction.
 */
void
Client::Begin()
{
    Debug("BEGIN [%lu, %lu]", client_id, t_id + 1);
    // Initialize data structures.
    txn.clear();
    // txn = Transaction();
    t_id++;
}

/* Returns the value corresponding to the supplied key. */
int Client::Get(const string &key, int idx, string &value, yield_t yield)
{
#if 0
    Debug("GET [%lu, %lu : %s]", client_id, t_id, key.c_str());
    txn.addReadSet(key, idx, 0);
    return REPLY_OK;
#else
#ifdef ZIP_MEASURE
    auto start = std::chrono::high_resolution_clock::now();
#endif
    Debug("GET [%lu, %lu : %s]", client_id, t_id, key.c_str());
    
    // Read your own writes, check the write set first.
    if (txn.getWriteSet().find(key) != txn.getWriteSet().end()) {
        value = txn.getWriteSet().find(key)->second;
        return REPLY_OK;
    }

    // Send the GET operation.
    zip::client::client::zipkat_get_request request;
    request.buffer = &ZiplogBuffer();
    request.timestamp = -1;
    auto& req = request.buffer->as<zip::api::zipkat_get>();
    const auto leng = key.length();
    req.message_type = zip::api::ZIPKAT_GET;
    req.client_id = client_id;
    //req.client_id = ziplogClient->client_id();

    req.gsn = 0;
    req.data_length = leng;
    req.mid = client_id;
    std::memcpy(req.key, key.data(), leng);
    Assert(req.length() < ZiplogBuffer().length());

    static std::string empty;

    //Assert(ziplogClient.get());

    replica_queues_[getShard(key)][sticky_replica_].send(*(request.buffer), req.length(), zipkat_get_dest_, true);

    while (request.timestamp.load(std::memory_order_acquire) == -1) {
        zip::util::recv_apply(logger, zipkat_get_recv_queue_,
                              [&] (zip::api::zipkat_get_response& get_ack) {
                                  const auto old_mid = get_ack.mid;
                                  logger.trace("Received zipkat_get_response from server (", getShard(key), ":", get_ack.replica_id, ") with GSN ", get_ack.gsn, " mid=", get_ack.mid, ", data_length=", get_ack.data_length);

                                  logger.trace("ack.mid=", get_ack.mid, ", it->mid=", req.mid, ", it->gsn=", req.gsn, ", ack.gsn=", get_ack.gsn);
                                  ZIP_ASSERT_EQ(old_mid, get_ack.mid, "ack.mid got changed &ack.mid=", &get_ack.mid);
                                  if (get_ack.mid == req.mid) {
                                      req.gsn = get_ack.gsn;
                                      request.timestamp.store(req.gsn, std::memory_order_relaxed);
                                  }
                              }
        );
#if ZIP_MEASURE
    auto start2 = std::chrono::high_resolution_clock::now();
#endif
        //yield();
#ifdef ZIP_MEASURE
    auto end2 = std::chrono::high_resolution_clock::now();
    hdr_record_value(hist_yield, zip::util::time_in_us(end2 - start2));
    if (++hdr_count_yield == 10000) {                     
        hdr_count_yield = 0;
        auto lat_50 = hdr_value_at_percentile(hist_yield, 50);
        auto lat_99 = hdr_value_at_percentile(hist_yield, 99);
        auto lat_999 = hdr_value_at_percentile(hist_yield, 99.9);
        auto mean = hdr_mean(hist_yield);
        std::cerr << "Client-yield (" << client_id << ") statistics: median latency: " << lat_50 << " us\t99% latency: " << lat_99 << " us\t99.9% latency: " << lat_999 << " us\tmean: " << mean << std::endl;;
    }
#endif
    }

    const auto timestamp = request.timestamp.load(std::memory_order_relaxed);
#ifdef ZIP_MEASURE
    auto end = std::chrono::high_resolution_clock::now();
    hdr_record_value(hist_get, zip::util::time_in_us(end - start));
    if (++hdr_count_get == 10000) {                     
        hdr_count_get = 0;
        auto lat_50 = hdr_value_at_percentile(hist_get, 50);
        auto lat_99 = hdr_value_at_percentile(hist_get, 99);
        auto lat_999 = hdr_value_at_percentile(hist_get, 99.9);
        auto mean = hdr_mean(hist_get);
        std::cerr << "Client-get (" << client_id << ") statistics: median latency: " << lat_50 << " us\t99% latency: " << lat_99 << " us\t99.9% latency: " << lat_999 << " us\tmean: " << mean << std::endl;;
    }
#endif
    if (timestamp != zip::api::zipkat_get_response::kKeyNotFound) {
        Debug("[%lu] Adding [%s] with ts %lu", client_id, key.c_str(), timestamp);
        txn.addReadSet(key, idx, timestamp);
        return REPLY_OK;
    } else {
        Debug("[%lu] %s not found", client_id, key.c_str());
        return REPLY_FAIL;
    }
#endif
}

/* Sets the value corresponding to the supplied key. */
int Client::Put(const string &key, int idx, const string &value)
{
#if 0
    return REPLY_OK;
#else
    Debug("PUT [%lu, %lu : %s]", client_id, t_id, key.c_str());
    // Update the write set.
    txn.addWriteSet(key, idx, value);
    return REPLY_OK;
#endif
}

// TODO: make better method name.
int Client::Prepare(yield_t yield)
{
#if 0
    return REPLY_OK;
#else
    Debug("PREPARE [%lu, %lu] ", client_id, t_id);
    zip::client::client::request request;
    request.buffer = &ZiplogBuffer();
    request.response.store(-1, std::memory_order_release);

    size_t txnLen = txn.serializedSize();
    auto& req = ZiplogBuffer().as<zip::api::storage_insert_after>();
    req.message_type = zip::api::STORAGE_INSERT_AFTER;
    req.client_id = client_id;
    req.gsn_after = 0;
    req.num_slots = 1;

    /*
    auto commit_req = reinterpret_cast<zip::api::zipkat_commit_request*>(req.data);
    commit_req->data_length = txnLen;
    commit_req->nr_reads = txn.getReadSet().size();
    commit_req->nr_writes = txn.getWriteSet().size();
    txn.serialize((char*)commit_req->data);
    //txn.serialize(buf);
     */
    req.data_length = 0;

    Assert(req.length() < ZiplogBuffer().length());
    Assert(ziplogClient.get());
    ziplogClient->insert_after(request);
    while (request.response.load(std::memory_order_relaxed) == -1) {
/*
#if ZIP_MEASURE
    auto start2 = std::chrono::high_resolution_clock::now();
#endif
        yield();
#ifdef ZIP_MEASURE
    auto end2 = std::chrono::high_resolution_clock::now();
    hdr_record_value(hist_yield, zip::util::time_in_us(end2 - start2));
    if (++hdr_count_yield == 10000) {                     
        hdr_count_yield = 0;
        auto lat_50 = hdr_value_at_percentile(hist_yield, 50);
        auto lat_99 = hdr_value_at_percentile(hist_yield, 99);
        auto lat_999 = hdr_value_at_percentile(hist_yield, 99.9);
        auto mean = hdr_mean(hist_yield);
        std::cerr << "Client-yield (" << client_id << ") statistics: median latency: " << lat_50 << " us\t99% latency: " << lat_99 << " us\t99.9% latency: " << lat_999 << " us\tmean: " << mean << std::endl;;
    }
#endif
*/
        yield();
    }

    Debug("[%lu] PREPARE gsn=%ld app_returns=%lu", client_id, request.response.load(std::memory_order_relaxed), request.application_return);
    return request.application_return;
#endif
}

/* Attempts to commit the ongoing transaction. */
bool Client::Commit(yield_t yield)
{
#if 0
    return true;
#else
#ifdef ZIP_MEASURE
    auto start = std::chrono::high_resolution_clock::now();
#endif
    int status = Prepare(yield);

#ifdef ZIP_MEASURE
    auto end = std::chrono::high_resolution_clock::now();
    hdr_record_value(hist_commit, zip::util::time_in_us(end - start));
    if (++hdr_count_commit == 10000) {                     
        hdr_count_commit = 0;
        auto lat_50 = hdr_value_at_percentile(hist_commit, 50);
        auto lat_99 = hdr_value_at_percentile(hist_commit, 99);
        auto lat_999 = hdr_value_at_percentile(hist_commit, 99.9);
        auto mean = hdr_mean(hist_commit);
        std::cerr << "Client-commit (" << client_id << ") statistics: median latency: " << lat_50 << " us\t99% latency: " << lat_99 << " us\t99.9% latency: " << lat_999 << " us\tmean: " << mean << std::endl;
    }
#endif
    if (status == REPLY_OK) {
        Debug("COMMIT [%lu, %lu]", client_id, t_id);
        return true;
    }

    Debug("ABORT [%lu, %lu]", client_id, t_id);
    return false;
#endif
}

/* Aborts the ongoing transaction. */
void
Client::Abort()
{
    Debug("ABORT [%lu]", t_id);
    // Do nothing.
}

/* Return statistics of most recent transaction. */
vector<int> Client::Stats()
{
    vector<int> v;
    return v;
}

unsigned long Client::getShard(std::string key){
    uint64_t hash = 5381;
    const char* str = key.c_str();
    for (unsigned int j = 0; j < key.length(); j++) {
        hash = ((hash << 5) + hash) + (uint64_t)str[j];
    }
    return hash % zip::consts::NUM_ZIPKAT_SHARDS;

}

} // namespace meerkatir
} // namespace meerkatstore
