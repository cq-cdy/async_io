
#include <functional>
#include <iostream>

#include "async_io.hpp"

using namespace talon;
using namespace talon::task;
using namespace std;
talon::IOHandler io;

// 读取处理函数，添加回写逻辑
void example_read_handler(KernelBufPtr kbf) {
    auto ret = kbf->io_ret_v();
    auto fd = kbf->io_ret_fd();
    printf("in example_read_handler ret = %d,fd = %d\n", ret, fd);
    printf("read data:");
    for (int i = 0; i < ret; i++) {
        printf("%c", kbf->buf_[i]);
    }
    printf("\n");
    // 反转字符串
    std::string original_str(kbf->buf_.begin(), kbf->buf_.end());
    std::string reversed_str = original_str;
    std::reverse(reversed_str.begin(), reversed_str.end());

    // 构建回写描述信息
    std::string write_desc =
        "peer fd = [" + std::to_string(fd) + "]: write back";
    std::string write_str = write_desc + reversed_str;

    // 创建新的 KernelBuf 并设置数据
    KernelBufPtr write_buf = std::make_shared<KernelBuf>(write_str.size());
    std::copy(write_str.begin(), write_str.end(), write_buf->buf_.begin());

    // 创建回写任务

    auto writeTask = createTaskWithHandler(fd);
    writeTask->set_task_type(TaskType::WRITE);
    writeTask->set_buffer_hardly(write_buf);
    io.addTask(writeTask);
    // auto res= writeTask->io_done();
    // printf("in w %d %d %s\n", res.io_ret(), res.event_fd(), res.err_msg().data());
}
// 在example_accpet_hanlder中设置READ任务为长连接
void example_accpet_hanlder(KernelBufPtr kbf) {
    auto ret = kbf->io_ret_v();
    auto fd = kbf->io_ret_fd();
    printf("New connection accepted on fd %d\n", ret);

    auto readTask = createTaskWithHandler(ret, example_read_handler);
    readTask->set_task_type(TaskType::READ);
    readTask->set_repeat_forever(true);  // 设置为长连接
    io.addTask(readTask);

    // auto res = readTask->io_done();
    // printf("%d %d %s",res.io_ret(),res.event_fd(),res.err_msg().data());
    // while(!io.addTask(readTask)){
    //     std::cout << readTask->get_debug_str();
    // }
}

int main() {
    io.init_tcp_server(8081);
    auto accpet_task =
        createTaskWithHandler(io.listen_fd(), example_accpet_hanlder);
    accpet_task->set_repeat_forever(true);
    accpet_task->set_task_type(TaskType::ACCEPT);
    io.addTask(accpet_task);
    auto res = accpet_task->io_done();

    sleep(2);

    sleep(1000000);
    return 0;
}
