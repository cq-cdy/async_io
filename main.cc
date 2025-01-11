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
    auto func = [](KernelBufPtr buffer, int a, string s) -> std::string {
        std::cout << "buffer size: " << buffer->size() << std::endl;
        std::cout << "a: " << a << std::endl;
        std::cout << "s: " << s << std::endl;
        return "ok";
    };
    auto sendTask = createTaskWithHandler(3, sendHandler);
    auto res = sendTask.excute_after_op(sendTask.get_buffer(), 10);
    std::cout << res << std::endl;

    auto task = createTaskWithHandler(4, func);
    auto res2 = task.excute_after_op(task.get_buffer(), 2, "hello");
    std::cout << res2;

    auto task2 = createTaskWithHandler(5, func2);
    A* p = task2.excute_after_op(task2.get_buffer());
    std::cout << "a: " << p->a << std::endl;
    std::cout << "s: " << p->s << std::endl;

    auto task3 = createTaskWithHandler(6);
    return 0;
}
