// -*- mode: c++; c-file-style: "k&r"; c-basic-offset: 4 -*-
/***********************************************************************
 *
 * store/benchmark/benchClient.cc:
 *   Benchmarking client for a distributed transactional store.
 *
 **********************************************************************/

#include "store/common/consts.h"
#include "store/common/truetime.h"
#include "store/meerkatstore/meerkatir/client.h"
#include "store/common/flags.h"

#include "network/buffer.h"
#include "network/manager.h"

#include <boost/fiber/all.hpp>
#include <csignal>
#include <fstream>
#include <signal.h>
#include <random>

#include <iostream>

using namespace std;

//#define ZIPKAT_SEPARATE_THREAD 1

#define EXP_BACKOFF 1
// This makes the maximum backoff time 5.12 ms, which is half of one epoch.
#define BACKOFF_MAX_EXP 8
#define BACKOFF_UNIT_TIME_US 20
//#define SORT_KEY 1

// Function to pick a random key according to some distribution.
uint32_t rand_key_zipf();

//bool ready = false;
//double *zipf;
vector<string> keys;
vector<std::uniform_int_distribution<uint32_t>> keys_distributions;

struct measurement {
    uint64_t nTransaction;
    timeval start;
    timeval end;
    bool status;
    int ttype;
    bool validated;
};
constexpr size_t kNumMeasurement = 1000000;

unsigned int zipf_dist()
{
    static int first = true;      // Static first time flag
    static double c = 0;          // Normalization constant
    static double *sum_probs;     // Pre-calculated sum of probabilities
    double z;                     // Uniform random number (0 < z < 1)
    int zipf_value;               // Computed exponential value to be returned
    int    i;                     // Loop counter
    int low, high, mid;           // Binary-search bounds

    // Compute normalization constant on first call only
    if (first == true)
    {
        for (i=1; i<=FLAGS_numKeys; i++)
            c = c + (1.0 / pow((double) i, FLAGS_zipf));
        c = 1.0 / c;

        sum_probs = new double[FLAGS_numKeys+1];
        sum_probs[0] = 0;
        for (i=1; i<=FLAGS_numKeys; i++) {
            sum_probs[i] = sum_probs[i-1] + c / pow((double) i, FLAGS_zipf);
        }
        first = false;
    }

    // Pull a uniform random number (0 < z < 1)
    do
    {
        z = (1.0 + rand())/RAND_MAX;
    }
    while ((z == 0) || (z == 1));

    // Map z to the value
    low = 1, high = FLAGS_numKeys, mid;
    do {
        mid = floor((low+high)/2);
        if (sum_probs[mid] >= z && sum_probs[mid-1] < z) {
            zipf_value = mid;
            break;
        } else if (sum_probs[mid] >= z) {
            high = mid-1;
        } else {
            low = mid+1;
        }
    } while (low <= high);

    // Assert that zipf_value is between 1 and N
    assert((zipf_value >=1) && (zipf_value <= FLAGS_numKeys));

    return(zipf_value-1);
}

struct TxnWorkload {
    int ttype;
    std::vector<uint32_t> keyIdx;
    timeval start;
};

ostream& operator<<(ostream& os, const TxnWorkload& wrk)
{
    os << "ttype=" << wrk.ttype << "\n";
    for (auto idx : wrk.keyIdx)
        os << idx << ",";
    os << "\n";
    return os;
}

void client_fiber_func(int thread_id, std::shared_ptr<zip::client::client> ziplogClient,
                       zip::network::manager* manager) {
//    vector<measurement> results(kNumMeasurement);
    vector<string> results;

    std::mt19937 core_gen;
    std::mt19937 replica_gen;
    //std::uniform_int_distribution<uint32_t> core_dis(0, FLAGS_numServerThreads - 1);
    //std::uniform_int_distribution<uint32_t> replica_dis(0, nReplicas - 1);
    std::random_device rd;
    uint8_t preferred_thread_id;
    uint32_t localReplica = -1;
    std::uniform_int_distribution<uint32_t> key_dis = std::uniform_int_distribution<uint32_t >(0, FLAGS_numKeys - 1);
    core_gen = std::mt19937(rd());
    replica_gen = std::mt19937(rd());
    std::mt19937 key_gen = std::mt19937(rd());

    auto rand_key = [&] () {
        if (FLAGS_zipf <= 0) {
            return key_dis(key_gen);
        }
        else {
            return zipf_dist();
        }
    };

    /*
    std::map<int, double> dist;
    for (int i = 0; i < 10000000; i++) {
        auto x = rand_key();
        if (dist.find(x) != dist.end()) {
            dist.find(x)->second++;
        } else {
            dist.insert({x, 1});
        }
    }
    auto y = dist.begin();
    for (auto x = 0; x != 30; x++) {
        std::cout << "Key: " << y->first << " prob: " << y->second / 10000000 << std::endl;
        y++;
    }
     */

    std::cout << "Flag Num Keys " << FLAGS_numKeys << std::endl;
    std::cout << "Zipfian Flag " << FLAGS_zipf << std::endl;

    // Open file to dump results
    uint32_t global_thread_id = thread_id;
    FILE* fp = fopen((FLAGS_logPath + "/client." + std::to_string(global_thread_id) + ".log").c_str(), "w");

    /*
     *CSV FORMATING, CONTINUE LATER
    FILE* fp = fopen((FLAGS_logPath + "/client." + std::to_string(global_thread_id) + ".csv").c_str(), "w");
    fprintf(fp, "%s", "txn,txn_id,start,end,latency,committed,txn_type,validated,promise_updated,hot_key\n");

    auto csv_format = "%d,%ld.%06ld,%ld.%06ld,%ld,%d,%d,%d,%d,%d,%d,%d\n";
    */
    std::cout << "Start RetwisClient-" << global_thread_id << std::endl;
    // Trying to distribute as equally as possible the clients on the
    // replica cores.

    //preferred_core_id = core_dis(core_gen);

    // pick the prepare and commit thread on the replicas in a round-robin fashion
    preferred_thread_id = global_thread_id % FLAGS_numServerThreads;

    // pick the replica and thread id for read in a round-robin fashion
    int global_preferred_read_thread_id  = global_thread_id % FLAGS_numServerThreads;
    int local_preferred_read_thread_id = global_preferred_read_thread_id;

    if (FLAGS_closestReplica == -1) {
        //localReplica =  (global_thread_id / nsthreads) % nReplicas;
        // localReplica = replica_dis(replica_gen);
        localReplica = global_preferred_read_thread_id;
    } else {
        localReplica = FLAGS_closestReplica;
    }

    //fprintf(stderr, "global_thread_id = %d; localReplica = %d\n", global_thread_id, localReplica);
    Assert(FLAGS_mode == "meerkatstore");
    auto client = std::make_unique<meerkatstore::meerkatir::Client>(
                                        FLAGS_numServerThreads, FLAGS_numShards,
                                        global_thread_id, ziplogClient, *manager);
    struct timeval t0, t1, t2;

    uint64_t nTransactions = 0;
    int tCount = 0;
    double tLatency = 0.0;
    int getCount = 0;
    double getLatency = 0.0;
    int commitCount = 0;
    double commitLatency = 0.0;
    string key, value;
    bool status;
    char buffer[100];
    string v (56, 'x'); //56 bytes

    gettimeofday(&t0, NULL);
    srand(global_thread_id * (t0.tv_sec + t0.tv_usec));

    int ret;
    FLAGS_secondsFromEpoch = t0.tv_sec;
#ifdef ZIP_MEASURE
    hdr_histogram* hist_wrk;
    hdr_init(1, 10000, 3, &hist_wrk);
    int hdr_count_wrk = 0;
#endif

    bool retry = false;
    int retry_exp = 0;
    TxnWorkload wrk;
    while (1) {
        if (!retry) {
            const int ttype = rand() % 100;
            int numKeys;
            if (ttype < 5) {
                numKeys = 3;
            } else if (ttype < 20) {
                numKeys = 2;
            } else if (ttype < 50) {
                numKeys = 5;
            } else {
                numKeys = 1 + rand() % 10;
            }

            wrk.ttype = ttype;
            wrk.keyIdx.clear();
            for (int i = 0; i < numKeys; ++i) {
                wrk.keyIdx.emplace_back(rand_key());
            }
#ifdef SORT_KEY
            sort(wrk.keyIdx.begin(), wrk.keyIdx.end());
#endif

            //std::cout << "client-" << global_thread_id << ": " << wrk;
            gettimeofday(&wrk.start, NULL);
        }
        status = true;

        gettimeofday(&t1, NULL);
        client->Begin();
        Interval interval;

        int ttype; // Transaction type for log
        if (wrk.ttype < 5) {
            // 5% - Add user transaction. 1,3
            int idx = wrk.keyIdx[0];
            if ((ret = client->Get(keys[idx], idx, value, boost::this_fiber::yield, interval))) {
                Warning("Aborting due to %s %d", keys[idx].c_str(), ret);
                status = false;
            }

            for (int i = 0; i < 3 && status; i++) {
                int idx = wrk.keyIdx[i];
                client->Put(keys[idx], idx, v);
            }
            ttype = 1;
        } else if (wrk.ttype < 20) {
            // 15% - Follow/Unfollow transaction. 2,2
            for (int i = 0; i < 2 && status; i++) {
                int idx = wrk.keyIdx[i];
                if ((ret = client->Get(keys[idx], idx, value, boost::this_fiber::yield, interval))) {
                    Warning("Aborting due to %s %d", keys[idx].c_str(), ret);
                    status = false;
                }
                client->Put(keys[idx], idx, v);
            }
            ttype = 2;
        } else if (wrk.ttype < 50) {
            // 30% - Post tweet transaction. 3,5
            for (int i = 0; i < 3 && status; i++) {
                int idx = wrk.keyIdx[i];
                if ((ret = client->Get(keys[idx], idx, value, boost::this_fiber::yield, interval))) {
                    Warning("Aborting due to %d %s %d", idx, keys[idx].c_str(), ret);
                    status = false;
                }
                client->Put(keys[idx], idx, v);
            }
            for (int i = 0; i < 2; i++) {
                int idx = wrk.keyIdx[i+3];
                client->Put(keys[idx], idx, v);
            }
            ttype = 3;
        } else {
            // 50% - Get followers/timeline transaction. rand(1,10),0
            for (int keyIdx : wrk.keyIdx) {
                if ((ret = client->Get(keys[keyIdx], keyIdx, value, boost::this_fiber::yield, interval, /* read_only */true))) {
                    Warning("Aborting due to %s %d", keys[keyIdx].c_str(), ret);
                    status = false;
                }
            }
            ttype = 4;
        }

        //gettimeofday(&t3, NULL);
        //fprintf(fp, "Begin commit\n");
        int retry_count_log;
        if (status) {
            status = client->Commit(boost::this_fiber::yield);
            if (status)  {
                retry_count_log = retry_exp;
                retry = false;
                retry_exp = 0;
            } else {
                retry = true;
#ifdef EXP_BACKOFF
                // backoff
                const int exp = std::min(++retry_exp, BACKOFF_MAX_EXP);
                const uint64_t backoff = std::uniform_int_distribution<uint64_t>(0UL, (1UL << exp) * BACKOFF_UNIT_TIME_US)(key_gen);
                //Info("backoff %lu us", backoff);
                boost::this_fiber::sleep_for(std::chrono::microseconds(backoff));
                retry_count_log = -1 * retry_exp;
#endif
            }

        }
        gettimeofday(&t2, NULL);
        //fprintf(fp, "Done commit\n");

        //commitCount++;
        //commitLatency += ((t2.tv_sec - t3.tv_sec)*1000000 + (t2.tv_usec - t3.tv_usec));

        // log only the transactions that finished in the interval we actually measure
        if ((t2.tv_sec >= FLAGS_secondsFromEpoch + FLAGS_warmup) &&
            (t2.tv_sec < FLAGS_secondsFromEpoch + FLAGS_duration - FLAGS_warmup)) {
            long latency = status ?
                (t2.tv_sec - wrk.start.tv_sec)*1000000 + (t2.tv_usec - wrk.start.tv_usec) :
                (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec - t1.tv_usec);
            /*

            if (status) {
                sprintf(buffer, csv_format, ++nTransactions, wrk.start.tv_sec,
                        wrk.start.tv_usec, t2.tv_sec, t2.tv_usec, latency, status?1:0, ttype, client->getValidation()?1:0, client->getPromiseNotUpdated()?1:0, client->getHotKey()?1:0);
            } else {
                sprintf(buffer, csv_format, ++nTransactions, t1.tv_sec,
                        t1.tv_usec, t2.tv_sec, t2.tv_usec, latency, status?1:0, ttype, client->getValidation()?1:0, client->getPromiseNotUpdated()?1:0, client->getHotKey()?1:0);
            }
            */

            if (status) {
                sprintf(buffer, "%d %ld.%06ld %ld.%06ld %ld %d %d %d %d %d %d %d\n", ++nTransactions, wrk.start.tv_sec,
                        wrk.start.tv_usec, t2.tv_sec, t2.tv_usec, latency, status?1:0, ttype, client->getFastValidation()?1:0, client->getPromiseNotUpdated()?1:0, client->getHotKey()?1:0);
            } else {
                sprintf(buffer, "%d %ld.%06ld %ld.%06ld %ld %d %d %d %d %d %d %d\n", ++nTransactions, t1.tv_sec,
                        t1.tv_usec, t2.tv_sec, t2.tv_usec, latency, status?1:0, ttype, client->getFastValidation()?1:0, client->getPromiseNotUpdated()?1:0, client->getHotKey()?1:0);
            }
            results.push_back(string(buffer));
            if (status) {
                tCount++;
                tLatency += latency;
            }

        }
        gettimeofday(&t1, NULL);
        if (((t1.tv_sec-t0.tv_sec)*1000000 + (t1.tv_usec-t0.tv_usec)) > FLAGS_duration*1000000) {
            std::cout << "yoyo I fucking finished" << std::endl;
                break;
        }
    }

    for (auto line : results) {
        fprintf(fp, "%s", line.c_str());
    }
    std::cout << "Overall latency " << tLatency/tCount << std::endl;
    std::cout << "Overall Tput " << tCount/(FLAGS_duration - 2*FLAGS_warmup) << std::endl;

    fprintf(fp, "# Commit_Ratio: %lf\n", (double)tCount/nTransactions);
    fprintf(fp, "# Overall_Latency: %lf\n", tLatency/tCount);
    fprintf(fp, "# Get: %d, %lf\n", getCount, getLatency/getCount);
    fprintf(fp, "# Commit: %d, %lf\n", commitCount, commitLatency/commitCount);
    fclose(fp);
    std::cout << "RetwisClient client-" << global_thread_id << " done\n";
}

void* client_thread_func(int ziplog_id, int cpu_id, zip::network::manager* manager) {
    // create the client fibers
    boost::fibers::fiber client_fibers[FLAGS_numClientFibers];

    // Use cores of NUMA1.

    printf("ziplog id=%d, cpu_id=%d, client_rate=%lu\n", ziplog_id, cpu_id, FLAGS_ziplogClientRate);

    auto ziplogClient = std::make_shared<zip::client::client>(
        *manager, kOrderAddr, ziplog_id, kZiplogShardId, cpu_id, FLAGS_ziplogClientRate);
    for (int i = 0; i < FLAGS_numClientFibers; i++) {
        boost::fibers::fiber f(
            client_fiber_func, ziplog_id * FLAGS_numClientFibers + i, ziplogClient, manager);
        client_fibers[i] = std::move(f);
    }

    for (int i = 0; i < FLAGS_numClientFibers; i++) {
        client_fibers[i].join();
        //printf("ziplog id=%d, done joinging i=%d\n", ziplog_id, i);
    }
    return NULL;
};


void segfault_sigaction(int signal, siginfo_t *si, void *arg)
{
    fprintf(stderr, "Caught segfault at address %p, code = %d\n", si->si_addr, si->si_code);
    exit(0);
}

int main(int argc, char **argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
#ifdef EXP_BACKOFF
    printf("EXP_BACKOFF enabled, BACKOFF_UNIT_TIME_US=%d, BACKOFF_MAX_EXP=%d\n", BACKOFF_UNIT_TIME_US, BACKOFF_MAX_EXP);
#endif
#ifdef SORT_KEY
    printf("SORT_KEY enabled\n");
#endif

/*
    struct sigaction sa;

    memset(&sa, 0, sizeof(struct sigaction));
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = segfault_sigaction;
    sa.sa_flags   = SA_SIGINFO;

    sigaction(SIGSEGV, &sa, NULL);
*/

    std::signal(SIGINT, [] (int signal) { std::cerr << "SIGINT caught\n"; exit(1); });

    // initialize the uniform distribution
    std::random_device rd;
    zipf_dist();
    // Read in the keys from a file.
    string key, value;
    ifstream in;
    in.open(FLAGS_keysFile);
    if (!in) {
        fprintf(stderr, "Could not read keys from: %s\n",
                FLAGS_keysFile.c_str());
        exit(0);
    }
    for (int i = 0; i < FLAGS_numKeys; i++) {
        getline(in, key);
        keys.push_back(key);
    }
    in.close();

    // Create the transport threads; each transport thread will run
    // FLAGS_numClientThreads client fibers
    // TODO: specify client/ziplog client ratio.
//    const auto ziplog_id = FLAGS_nhost * FLAGS_numClientThreads;
//    const auto cpu_id = 1;
    std::vector<std::unique_ptr<zip::network::manager>> managers;

    std::vector<std::thread> client_thread_arr(FLAGS_numClientThreads);
    const auto id_base = FLAGS_nhost * FLAGS_numClientThreads;
    for (size_t i = 0; i < FLAGS_numClientThreads; i++) {
/*
        if (i % 2 == 0) {
            printf("thread id=%d at %s\n", i, zip::consts::rdma::DEFAULT_DEVICE1);
            managers.emplace_back(std::make_unique<zip::network::manager>(zip::consts::rdma::DEFAULT_DEVICE1, zip::consts::rdma::DEFAULT_PORT, 1));
        } else {
*/
            printf("thread id=%d at %s\n", i, zip::consts::rdma::DEFAULT_DEVICE);
            managers.emplace_back(std::make_unique<zip::network::manager>(zip::consts::rdma::DEFAULT_DEVICE, zip::consts::rdma::DEFAULT_PORT, zip::consts::rdma::DEFAULT_GID));
        int ziplog_core;
        int txn_core;
#ifdef ZIPKAT_SEPARATE_THREAD

#if 0
        assert(FLAGS_numClientThreads <= 4); // Run with FLAGS_numClientThreads <= 4;
        if (i < 2) {
            // *2 for using the core of the same NUMA, *2 again because one ziplog client uses one cores
            ziplog_core = 3 * 2 * i + 1;
            txn_core = 3 * 2 * i + 5;
        } else {
            ziplog_core = 3 * 2 * (i - 2);
            txn_core = 3 * 2 * (i - 2) + 4;
        }
#else
        if (i < 4) {
            // *2 for using the core of the same NUMA, *2 again because one ziplog client uses one cores
            ziplog_core = 3 * 2 * i + 1;
            txn_core = 3 * 2 * i + 5;
        } else {
            ziplog_core = 3 * 2 * (i - 3);
            txn_core = 3 * 2 * (i - 3) + 4;
        }
#endif

#else
        // TODO: use const for determining the 4, it's basically related to # of real (not hyper) cores on a machine.
#if 1
        if (i < 4) {
            ziplog_core = 2 * 2 * i + 1;
            txn_core = 2 * 2 * i + 3;
        } else {
            ziplog_core = 2 * 2 * (i - 4);
            txn_core = 2 * 2 * (i - 4) + 2;
        }
#else
        // Try using hyper cores.
        ziplog_core = 16 + 2 * i + 1; //2 * i + 1;
        txn_core = 2 * i + 1; //16 + 2 * i + 1;
#endif
#endif
        client_thread_arr[i] = std::thread(client_thread_func, id_base + i, ziplog_core, managers.back().get());
        zip::util::pin_thread(client_thread_arr[i], txn_core);
        // client_thread_arr[i] = std::thread(client_fiber_func, i, ziplogClient);
        // uint8_t idx = i/2 + (i % 2) * 12;
        // erpc::bind_to_core(client_thread_arr[i], 0, i);
    }
    for (auto &thread : client_thread_arr) thread.join();

    return 0;
}

/*
uint32_t rand_key_zipf()
{
    // Zipf-like selection of keys.
    if (!ready) {
        zipf = new double[FLAGS_numKeys];

        double c = 0.0;
        for (int i = 1; i <= FLAGS_numKeys; i++) {
            c = c + (1.0 / pow((double) i, FLAGS_zipf));
        }
        c = 1.0 / c;

        double sum = 0.0;
        for (int i = 1; i <= FLAGS_numKeys; i++) {
            sum += (c / pow((double) i, FLAGS_zipf));
            zipf[i-1] = sum;
        }
        ready = true;
    }

    double random = 0.0;
    while (random == 0.0 || random == 1.0) {
        random = (1.0 + rand())/RAND_MAX;
    }

    // binary search to find key;
    int l = 0, r = FLAGS_numKeys, mid;
    while (l < r) {
        mid = (l + r) / 2;
        if (random > zipf[mid]) {
            l = mid + 1;
        } else if (random < zipf[mid]) {
            r = mid - 1;
        } else {
            break;
        }
    }
    return mid;
}
*/

