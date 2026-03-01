#include <iostream>
#include "router.h"

void test_handler1() { printf("Matched GET age\n"); }
void test_handler2() { printf("Matched GET home\n"); }
void test_handler3() { printf("Matched POST profile\n"); }
void test_handler4() { printf("Matched PUT user\n"); }

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
    router.addRoute(HttpMethod::PUT, "/user/<uid>", (void *) test_handler4);
    if (router.match("put /user/admin", res)) {
        std::string uid = res.params["uid"];
        printf("Request: put /user/admin -> uid=%s, ", uid.c_str());
        ((void (*)()) res.handler)();
    }

    return 0;
}
