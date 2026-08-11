#pragma ocne

#include "helper.h"

#include <vector>
#include <queue>
#include <mutex>
#include <pqxx/pqxx>
#include <string>

class ThreadPool;

class Database {
public:
    Database();
    static void InitPool(int threads);
    void Init();

    static Database& GetDatabase() {
        thread_local Database database;
        thread_local bool initialized = false;
        if (!initialized) {
            database.Init();
            initialized = true;
        }
        return database;
    }

    static long InsertPage(const std::string& url, const std::string& title, const std::string& description, long contentHash, const std::string& favicon, const long documentLength);
    // takes ownership of the words vector
    static void IndexerAddToDB(long urlId, const std::string& url, const std::vector<WordData>& words);
    static void IncreaseAuthority(std::string url);

    static size_t QueueSize();
    // called first on program start to populate the queue
    static void InitPopulate();
    // Get queue.front(), POPs it from queue, and will populate if queue becomes empty
    static std::string QueueGet();

    // Handles if urls.size() becomes greater than {maxQueueSize}
    void UrlsAdd(const std::string& url);

private:
    // gets top {maxQueueSize} from queue DB and inserts into queue
    static void PopulateSiteQueue();

    pqxx::connection cx;

    static void QueueAdd(const std::string& v);

    static void UrlsClear();
    static std::vector<std::string> UrlsCopy();
    static size_t UrlsSize();
    static std::string UrlsGet(size_t i);

    static inline std::mutex queueMutex;
    static inline std::mutex urlsMutex;
    static inline std::mutex urlMutex;
    static inline int maxQueueSize = 5;
    static inline std::queue<std::string> queue;
    static inline std::vector<std::string> urls;

    static inline ThreadPool* threadPool = nullptr;
};
