
#include <cstdio>
#include <functional>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

enum class HttpMethod { GET, POST, PUT, DELETE_ };

struct StringView {
    const char *data_ptr; // 改名避免与函数 data() 冲突
    size_t size_val;

    // --- 构造函数 ---
    StringView() : data_ptr(nullptr), size_val(0) {
    }

    StringView(const char *d, size_t s) : data_ptr(d), size_val(s) {
    }

    StringView(const std::string &s) : data_ptr(s.data()), size_val(s.size()) {
    }

    StringView(const char *d) : data_ptr(d), size_val(d ? std::strlen(d) : 0) {
    }

    // --- 基础访问接口 ---
    const char *data() const { return data_ptr; }
    size_t size() const { return size_val; }
    bool empty() const { return size_val == 0; }

    // --- 下标访问 ---
    // 注意：不检查越界，追求极致性能（类似 std::string_view）
    const char &operator[](size_t pos) const { return data_ptr[pos]; }

    // --- 迭代器 (为了支持 range-based for 循环) ---
    const char *begin() const { return data_ptr; }
    const char *end() const { return data_ptr + size_val; }

    // --- 常用修改器 (修改窗口，不修改数据) ---
    void remove_prefix(size_t n) {
        data_ptr += n;
        size_val -= n;
    }

    void remove_suffix(size_t n) { size_val -= n; }

    // --- 比较操作 ---
    int compare(StringView other) const {
        size_t rlen = std::min(size_val, other.size_val);
        int ret = std::memcmp(data_ptr, other.data_ptr, rlen);
        if (ret == 0) {
            if (size_val < other.size_val)
                return -1;
            if (size_val > other.size_val)
                return 1;
        }
        return ret;
    }

    bool operator==(StringView other) const { return compare(other) == 0; }
    bool operator!=(StringView other) const { return !(*this == other); }
};

// --- 为了方便 cout 直接打印 ---
inline std::ostream &operator<<(std::ostream &os, StringView sv) { return os.write(sv.data(), sv.size()); }

struct RouteResult {
    std::unordered_map<std::string, std::string> params;
    void *handler = nullptr;

    void clear() {
        params.clear();
        handler = nullptr;
    }
};


class RadixRouter {
public:
    typedef void *Handler;

    bool addRoute(const HttpMethod &method, const StringView &path, Handler handler);

    bool addRoute(const std::string &combined, Handler handler);

    bool match(HttpMethod method, const StringView &path, RouteResult &out);

    bool match(const std::string &combined, RouteResult &out);

    void dump(std::string &buf);

private:
    std::mutex mutex_;

    enum NodeType { STATIC, PARAM };

    struct Node {
        std::string part;
        NodeType type;

        std::unordered_map<std::string, Node *> static_children;
        std::unordered_map<std::string, Node *> param_children;

        Handler handler = nullptr;

        Node(const std::string &p, NodeType t) : part(p), type(t) {
        }
    };

    std::unordered_map<HttpMethod, Node *> roots;

private:
    static void split(const StringView &path, std::vector<StringView> &out);

    static std::string parseParam(const std::string &part);

    static bool parseCombined(const std::string &combined, HttpMethod &method, std::string &path);

    void dumpRecursive(Node *node, const std::string &prefix, std::string &buf);

    static std::string methodToString(HttpMethod method);

    bool matchRecursive(Node *node, const std::vector<StringView> &parts, size_t index, RouteResult &out);
};
