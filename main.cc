// #include <iostream>

// #include "async_io.hpp"
// int main() {
//     using namespace talon;
//     using namespace talon ::task;
//     using namespace std;
//     using KernelBufPtr = task::AsyncTask<>::KernelBufPtr;
//     auto handler = [&](KernelBufPtr&, double) -> std::string { return "ok";
//     };

//     // 使用auto创建task实例，类型会根据上下文自动推导
//     auto task = AsyncTask<TaskType::ACCEPT, std::string,
//     double>::createTaskInstance(4);

//     task->set_handler(handler);
//     std::cout << task->excute_after_op(task->get_buffer(), 2.0);

// }
#include <functional>
#include <iostream>

#include "async_io.hpp"

using namespace talon;
using namespace talon::task;
using namespace std;
bool sendHandler(KernelBufPtr buffer, int length) {
    auto buffer_ptr = buffer->data();
    for (int i = 0; i < length; i++) {
    }
    return true;
}
struct A {
    int a;
    string s;
};

A* func2(KernelBufPtr buffer) {
    A* p = new A();
    p->a = 10;
    p->s = "hello";
    return p;
}
int main() {
    auto func = [](KernelBufPtr buffer, int a, std::string s) -> std::string {
        std::cout << "buffer size: " << buffer->size() << std::endl;
        std::cout << "a: " << a << std::endl;
        std::cout << "s: " << s << std::endl;
        std::cout << "buffer: " << buffer->data() << std::endl;
        return "ok";
    };

    auto ptr = createTaskWithHandler(3, func, 2, std::string("hello"));
    auto task = createTaskWithHandler(4, func2);
    auto res = task->excute_after_op();
    std::cout << res->a;
    std::cout << res->s;

    return 0;
}
