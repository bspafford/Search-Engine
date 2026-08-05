#pragma ocne

#include <vector>
#include <queue>
#include <mutex>
#include <pqxx/pqxx>

class Database {
public:
    Database();
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

    long InsertPage(const std::string& url, const std::string& title, const std::string& description, long contentHash, const std::string& favicon);
    void IndexerAddToDB(long urlId, const std::string& url, std::unordered_map<std::string, int> counts);

    static size_t QueueSize();
    // Get queue.front(), POPs it from queue, and will populate if queue becomes empty
    std::string QueueGet();

    // Handles if urls.size() becomes greater than {maxQueueSize}
    void UrlsAdd(const std::string& url);

private:
    // gets top {maxQueueSize} from queue DB and inserts into queue
    void PopulateSiteQueue();

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
};
