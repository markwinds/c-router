#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include "router.h"

void test_handler1() { printf("Matched GET age\n"); }
void test_handler2() { printf("Matched GET home\n"); }
void test_handler3() { printf("Matched POST profile\n"); }
void test_handler4() { printf("Matched PUT user\n"); }
void test_handler5() { printf("Matched GET post\n"); }

int main() {
    RadixRouter router;

    // 使用新格式注册
    if (router.addRoute("GET /hello/<name>/age", (void *) test_handler1)) {
        printf("Successfully registered: GET /hello/<name>/age\n");
    }

    // 测试重复注册
    if (!router.addRoute("GET /hello/<name>/age", (void *) test_handler1)) {
        printf("Failed to register (duplicate): GET /hello/<name>/age\n");
    }

    router.addRoute("get /hello/<name>/home", (void *) test_handler2);
    router.addRoute("POST   /hello/<id>/profile", (void *) test_handler3);
    router.addRoute(HttpMethod::PUT, "/user/<uid>", (void *) test_handler4);

    // 增加多个参数的测试用例
    router.addRoute("GET /user/<uid>/post/<pid>", (void *) test_handler5);
    router.addRoute("GET /a/<b>/c/<d>/e", (void *) test_handler5);

    // 测试 dump 接口
    printf("\nAll registered routes:\n");
    std::string buf;
    router.dump(buf);
    printf("%s\n", buf.c_str());

    RouteResult res;

    // 测试用例 1: 正常匹配
    printf("Testing match operations:\n");
    if (router.match("GET /hello/alice/age", res)) {
        std::string name = res.params["name"];
        printf("Request: GET /hello/alice/age -> name=%s, ", name.c_str());
        ((void (*)()) res.handler)();
    }

    // 测试用例 2: 大小写不敏感匹配
    if (router.match("gEt /hello/bob/home", res)) {
        std::string name = res.params["name"];
        printf("Request: gEt /hello/bob/home -> name=%s, ", name.c_str());
        ((void (*)()) res.handler)();
    }

    // 测试用例 3: 多个空格匹配
    if (router.match("POST    /hello/123/profile", res)) {
        std::string id = res.params["id"];
        printf("Request: POST /hello/123/profile -> id=%s, ", id.c_str());
        ((void (*)()) res.handler)();
    }

    // 测试用例 4: 混合使用
    if (router.match("put /user/admin", res)) {
        std::string uid = res.params["uid"];
        printf("Request: put /user/admin -> uid=%s, ", uid.c_str());
        ((void (*)()) res.handler)();
    }

    // 测试用例 5: 多个参数匹配
    if (router.match("GET /user/99/post/1024", res)) {
        printf("Request: GET /user/99/post/1024 -> uid=%s, pid=%s, ",
               res.params["uid"].c_str(), res.params["pid"].c_str());
        ((void (*)()) res.handler)();
    }

    // 测试用例 6: 更多层级的参数匹配
    if (router.match("GET /a/val1/c/val2/e", res)) {
        printf("Request: GET /a/val1/c/val2/e -> b=%s, d=%s, ",
               res.params["b"].c_str(), res.params["d"].c_str());
        ((void (*)()) res.handler)();
    }

    // 多线程并发测试
    printf("\nStarting multi-threaded stress test...\n");
    std::atomic<int> success_count{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&router, &success_count, i]() {
            for (int j = 0; j < 1000; ++j) {
                RouteResult r;
                if (router.match("GET /hello/user_" + std::to_string(i) + "/age", r)) {
                    success_count++;
                }
            }
        });
    }

    // 同时尝试注册新路由
    std::thread writer([&router]() {
        for (int i = 0; i < 100; ++i) {
            router.addRoute("GET /dynamic/" + std::to_string(i), (void *) test_handler1);
        }
    });

    for (auto &t: threads) t.join();
    writer.join();

    printf("Multi-threaded test finished. Successful matches: %d\n", success_count.load());

    return 0;
}
