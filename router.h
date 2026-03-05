/**
 * 基于radix tree实现高性能路由 支持解析url中的参数
 * 后续看需求可以继续添加功能
 * 1.支持匹配通配符
 */

#pragma once

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

enum class HttpMethod
{
    GET,
    POST,
    PUT,
    DELETE_
};

// c++17才标准库才支持stringView,当前项目使用c++11,简单实现
struct StringView
{
    const char* data_ptr;   // 改名避免与函数 data() 冲突
    size_t      size_val;

    // --- 构造函数 ---
    StringView()
        : data_ptr(nullptr)
        , size_val(0)
    {}

    StringView(const char* d, size_t s)
        : data_ptr(d)
        , size_val(s)
    {}

    StringView(const std::string& s)
        : data_ptr(s.data())
        , size_val(s.size())
    {}

    StringView(const char* d)
        : data_ptr(d)
        , size_val(d ? std::strlen(d) : 0)
    {}

    // --- 基础访问接口 ---
    const char* data() const
    {
        return data_ptr;
    }
    size_t size() const
    {
        return size_val;
    }
    bool empty() const
    {
        return size_val == 0;
    }

    // --- 下标访问 ---
    // 注意：不检查越界，追求极致性能（类似 std::string_view）
    const char& operator[](size_t pos) const
    {
        return data_ptr[pos];
    }

    // --- 迭代器 (为了支持 range-based for 循环) ---
    const char* begin() const
    {
        return data_ptr;
    }
    const char* end() const
    {
        return data_ptr + size_val;
    }

    // --- 常用修改器 (修改窗口，不修改数据) ---
    void remove_prefix(size_t n)
    {
        data_ptr += n;
        size_val -= n;
    }

    void remove_suffix(size_t n)
    {
        size_val -= n;
    }

    // --- 转换接口 ---
    std::string to_string() const
    {
        return {data_ptr, size_val};
    }

    // --- 比较操作 ---
    int compare(StringView other) const
    {
        size_t rlen = std::min(size_val, other.size_val);
        int    ret  = std::memcmp(data_ptr, other.data_ptr, rlen);
        if (ret == 0)
        {
            if (size_val < other.size_val)
                return -1;
            if (size_val > other.size_val)
                return 1;
        }
        return ret;
    }

    bool operator==(StringView other) const
    {
        return compare(other) == 0;
    }
    bool operator!=(StringView other) const
    {
        return !(*this == other);
    }
};

// --- 为了方便 cout 直接打印 ---
inline std::ostream& operator<<(std::ostream& os, StringView sv)
{
    return os.write(sv.data(), sv.size());
}

template<typename HandlerType>
struct RouteResult
{
    std::unordered_map<std::string, std::string> params;
    HandlerType                                  handler = HandlerType();

    void clear()
    {
        params.clear();
        handler = HandlerType();
    }

    std::string getParam(const std::string& key, const std::string& defaultValue = "") const
    {
        auto it = params.find(key);
        if (it != params.end())
        {
            return it->second;
        }
        return defaultValue;
    }
};

template<typename HandlerType>
class RadixRouter
{
public:
    typedef HandlerType Handler;

    RadixRouter() = default;

    RadixRouter(const RadixRouter& other)
    {
        copyFrom(other);
    }

    RadixRouter& operator=(const RadixRouter& other)
    {
        if (this != &other)
        {
            copyFrom(other);
        }
        return *this;
    }

    ~RadixRouter()
    {
        clear();
    }

    bool addRoute(const HttpMethod& method, const StringView& path, Handler handler)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (roots.find(method) == roots.end())
        {
            roots[method] = new Node("", STATIC);
        }

        Node* node = roots[method];

        std::vector<StringView> parts;
        split(path, parts);

        for (size_t i = 0; i < parts.size(); ++i)
        {
            std::string part(parts[i].data(), parts[i].size());

            std::string param = parseParam(part);

            if (!param.empty())
            {
                if (node->param_children.count(param) == 0)
                {
                    node->param_children[param] = new Node(part, PARAM);
                }
                node = node->param_children[param];
                continue;
            }

            if (node->static_children.count(part) == 0)
            {
                node->static_children[part] = new Node(part, STATIC);
            }

            node = node->static_children[part];
        }

        if (node->handler)
        {
            return false;   // Already registered
        }

        node->handler = handler;
        return true;
    }

    bool addRoute(const std::string& combined, Handler handler)
    {
        HttpMethod  method;
        std::string path;
        if (parseCombined(combined, method, path))
        {
            return addRoute(method, StringView(path), handler);
        }
        return false;
    }

    bool match(HttpMethod method, const StringView& path, RouteResult<HandlerType>& out)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        out.clear();
        if (roots.find(method) == roots.end())
        {
            return false;
        }

        std::vector<StringView> parts;
        split(path, parts);

        return matchRecursive(roots[method], parts, 0, out);
    }

    bool match(const std::string& combined, RouteResult<HandlerType>& out)
    {
        HttpMethod  method;
        std::string path;
        if (parseCombined(combined, method, path))
        {
            return match(method, StringView(path), out);
        }
        return false;
    }

    void dump(std::string& buf)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto const& pair : roots)
        {
            std::string method = methodToString(pair.first);
            dumpRecursive(pair.second, method, buf);
        }
    }

private:
    mutable std::mutex mutex_;

    enum NodeType
    {
        STATIC,
        PARAM
    };

    struct Node
    {
        std::string part;
        NodeType    type;

        std::unordered_map<std::string, Node*> static_children;
        std::unordered_map<std::string, Node*> param_children;

        Handler handler = Handler();

        Node(const std::string& p, NodeType t)
            : part(p)
            , type(t)
        {}

        Node(const Node& other)
            : part(other.part)
            , type(other.type)
            , handler(other.handler)
        {
            for (auto const& pair : other.static_children)
            {
                static_children[pair.first] = new Node(*pair.second);
            }
            for (auto const& pair : other.param_children)
            {
                param_children[pair.first] = new Node(*pair.second);
            }
        }

        ~Node()
        {
            for (auto const& pair : static_children)
            {
                delete pair.second;
            }
            for (auto const& pair : param_children)
            {
                delete pair.second;
            }
        }
    };

    std::unordered_map<HttpMethod, Node*> roots;

private:
    static void split(const StringView& path, std::vector<StringView>& out)
    {
        size_t start = 0;

        for (size_t i = 0; i <= path.size(); ++i)
        {
            if (i == path.size() || path[i] == '/')
            {
                if (i > start)
                {
                    out.emplace_back(path.data() + start, i - start);
                }
                start = i + 1;
            }
        }
    }

    static std::string parseParam(const std::string& part)
    {
        if (part.size() >= 2)
        {
            if ((part[0] == '<' && part.back() == '>') || (part[0] == '{' && part.back() == '}'))
            {
                return part.substr(1, part.size() - 2);
            }
        }
        return "";
    }

    static bool parseCombined(const std::string& combined, HttpMethod& method, std::string& path)
    {
        size_t start = combined.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
            return false;

        size_t end_method = combined.find_first_of(" \t\r\n", start);
        if (end_method == std::string::npos)
            return false;

        std::string method_str = combined.substr(start, end_method - start);
        for (auto& c : method_str)
            c = (char)std::toupper((unsigned char)c);

        if (method_str == "GET")
            method = HttpMethod::GET;
        else if (method_str == "POST")
            method = HttpMethod::POST;
        else if (method_str == "PUT")
            method = HttpMethod::PUT;
        else if (method_str == "DELETE")
            method = HttpMethod::DELETE_;
        else
            return false;

        size_t start_path = combined.find_first_not_of(" \t\r\n", end_method);
        if (start_path == std::string::npos)
        {
            path = "/";   // 默认路径为根
        }
        else
        {
            path            = combined.substr(start_path);
            // 去除路径末尾的空白字符
            size_t end_path = path.find_last_not_of(" \t\r\n");
            if (end_path != std::string::npos)
            {
                path.erase(end_path + 1);
            }
        }
        return true;
    }

    void dumpRecursive(Node* node, const std::string& prefix, std::string& buf)
    {
        std::string current_path = prefix;
        if (node->part.empty())
        {
            if (current_path.empty() || current_path.back() != ' ')
            {
                current_path += " ";
            }
        }
        else
        {
            if (!current_path.empty() && current_path.back() != '/' && current_path.back() != ' ')
            {
                current_path += "/";
            }
            if (!current_path.empty() && current_path.back() == ' ')
            {
                current_path += "/";
            }
            current_path += node->part;
        }

        if (node->handler)
        {
            buf += current_path + "\n";
        }

        for (auto const& pair : node->static_children)
        {
            dumpRecursive(pair.second, current_path, buf);
        }

        for (auto const& pair : node->param_children)
        {
            dumpRecursive(pair.second, current_path, buf);
        }
    }

    static std::string methodToString(HttpMethod method)
    {
        switch (method)
        {
        case HttpMethod::GET: return "GET";
        case HttpMethod::POST: return "POST";
        case HttpMethod::PUT: return "PUT";
        case HttpMethod::DELETE_: return "DELETE";
        default: return "UNKNOWN";
        }
    }

    bool matchRecursive(Node* node, const std::vector<StringView>& parts, size_t index, RouteResult<HandlerType>& out)
    {
        if (index == parts.size())
        {
            if (node->handler)
            {
                out.handler = node->handler;
                return true;
            }
            return false;
        }

        StringView part = parts[index];

        // 1️⃣ 静态优先
        // 优化：避免创建临时 std::string 进行查找。在 C++11 中，unordered_map::find 必须传入 key 类型。
        // 但我们可以先通过一些方式减少分配，或者在注册时就做好索引。
        // 这里目前最简单且兼容 C++11 的方式是：
        std::string part_str(part.data(), part.size());
        auto        it_static = node->static_children.find(part_str);
        if (it_static != node->static_children.end())
        {
            if (matchRecursive(it_static->second, parts, index + 1, out))
            {
                return true;
            }
        }

        // 2️⃣ 参数匹配（递归回溯）
        for (auto const& p : node->param_children)
        {
            // 记录当前参数，尝试向下匹配
            out.params[p.first] = part_str;
            if (matchRecursive(p.second, parts, index + 1, out))
            {
                return true;
            }
            out.params.erase(p.first);
        }

        return false;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& pair : roots)
        {
            delete pair.second;
        }
        roots.clear();
    }

    void copyFrom(const RadixRouter& other)
    {
        std::lock(mutex_, other.mutex_);
        std::lock_guard<std::mutex> lock1(mutex_, std::adopt_lock);
        std::lock_guard<std::mutex> lock2(other.mutex_, std::adopt_lock);

        for (auto& pair : roots)
        {
            delete pair.second;
        }
        roots.clear();

        for (auto const& pair : other.roots)
        {
            roots[pair.first] = new Node(*pair.second);
        }
    }
};
