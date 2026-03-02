#include "router.h"

void RadixRouter::split(const StringView &path, std::vector<StringView> &out) {
    size_t start = 0;

    for (size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            if (i > start) {
                out.emplace_back(path.data() + start, i - start);
            }
            start = i + 1;
        }
    }
}

std::string RadixRouter::parseParam(const std::string &part) {
    if (part.size() >= 2) {
        if ((part[0] == '<' && part.back() == '>') || (part[0] == '{' && part.back() == '}')) {
            return part.substr(1, part.size() - 2);
        }
    }
    return "";
}

bool RadixRouter::addRoute(const HttpMethod &method, const StringView &path, Handler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (roots.find(method) == roots.end()) {
        roots[method] = new Node("", STATIC);
    }

    Node *node = roots[method];

    std::vector<StringView> parts;
    split(path, parts);

    for (size_t i = 0; i < parts.size(); ++i) {
        std::string part(parts[i].data(), parts[i].size());

        std::string param = parseParam(part);

        if (!param.empty()) {
            if (node->param_children.count(param) == 0) {
                node->param_children[param] = new Node(part, PARAM);
            }
            node = node->param_children[param];
            continue;
        }

        if (node->static_children.count(part) == 0) {
            node->static_children[part] = new Node(part, STATIC);
        }

        node = node->static_children[part];
    }

    if (node->handler) {
        return false; // Already registered
    }

    node->handler = handler;
    return true;
}

bool RadixRouter::addRoute(const std::string &combined, Handler handler) {
    HttpMethod method;
    std::string path;
    if (parseCombined(combined, method, path)) {
        // 由于 path 是局部变量，StringView 引用它，所以必须在这里完成 addRoute
        return addRoute(method, StringView(path.data(), path.size()), handler);
    }
    return false;
}

bool RadixRouter::match(HttpMethod method, const StringView &path, RouteResult &out) {
    std::lock_guard<std::mutex> lock(mutex_);
    out.clear();

    if (roots.find(method) == roots.end())
        return false;

    std::vector<StringView> parts;
    split(path, parts);

    return matchRecursive(roots[method], parts, 0, out);
}

bool RadixRouter::match(const std::string &combined, RouteResult &out) {
    HttpMethod method;
    std::string path;
    if (parseCombined(combined, method, path)) {
        return match(method, StringView(path), out);
    }
    return false;
}

bool RadixRouter::matchRecursive(Node *node, const std::vector<StringView> &parts, size_t index, RouteResult &out) {
    if (index == parts.size()) {
        if (node->handler) {
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
    auto it_static = node->static_children.find(part_str);
    if (it_static != node->static_children.end()) {
        if (matchRecursive(it_static->second, parts, index + 1, out)) {
            return true;
        }
    }

    // 2️⃣ 参数匹配（递归回溯）
    for (auto const &p: node->param_children) {
        // 记录当前参数，尝试向下匹配
        out.params[p.first] = part_str;
        if (matchRecursive(p.second, parts, index + 1, out)) {
            return true;
        }
        out.params.erase(p.first);
    }

    return false;
}


bool RadixRouter::parseCombined(const std::string &combined, HttpMethod &method, std::string &path) {
    size_t start = combined.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return false;

    size_t end_method = combined.find_first_of(" \t\r\n", start);
    if (end_method == std::string::npos) return false;

    std::string method_str = combined.substr(start, end_method - start);
    for (auto &c: method_str) c = (char) std::toupper((unsigned char) c);

    if (method_str == "GET") method = HttpMethod::GET;
    else if (method_str == "POST") method = HttpMethod::POST;
    else if (method_str == "PUT") method = HttpMethod::PUT;
    else if (method_str == "DELETE") method = HttpMethod::DELETE_;
    else return false;

    size_t start_path = combined.find_first_not_of(" \t\r\n", end_method);
    if (start_path == std::string::npos) {
        path = "/"; // 默认路径为根
    } else {
        path = combined.substr(start_path);
        // 去除路径末尾的空白字符
        size_t end_path = path.find_last_not_of(" \t\r\n");
        if (end_path != std::string::npos) {
            path.erase(end_path + 1);
        }
    }
    return true;
}

void RadixRouter::dump(std::string &buf) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto const &pair: roots) {
        std::string method = methodToString(pair.first);
        dumpRecursive(pair.second, method, buf);
    }
}

void RadixRouter::dumpRecursive(Node *node, const std::string &prefix, std::string &buf) {
    std::string current_path = prefix;
    if (node->part.empty()) {
        if (current_path.empty() || current_path.back() != ' ') {
            current_path += " ";
        }
    } else {
        if (!current_path.empty() && current_path.back() != '/' && current_path.back() != ' ') {
            current_path += "/";
        }
        if (!current_path.empty() && current_path.back() == ' ') {
            current_path += "/";
        }
        current_path += node->part;
    }

    if (node->handler) {
        buf += current_path + "\n";
    }

    for (auto const &pair: node->static_children) {
        dumpRecursive(pair.second, current_path, buf);
    }

    for (auto const &pair: node->param_children) {
        dumpRecursive(pair.second, current_path, buf);
    }
}

std::string RadixRouter::methodToString(HttpMethod method) {
    switch (method) {
        case HttpMethod::GET: return "GET";
        case HttpMethod::POST: return "POST";
        case HttpMethod::PUT: return "PUT";
        case HttpMethod::DELETE_: return "DELETE";
        default: return "UNKNOWN";
    }
}

