#include <fcntl.h>
#include <iostream>

#include "async_io.hpp"

using namespace talon;
using namespace talon::task;
using namespace std;

talon::IOHandler io;

void example_read_handler(KernelBufPtr kbf) {
    auto num = kbf->io_ret_v();
    auto fd = kbf->io_ret_fd();
    printf("in example_read_handler ret = %d, fd = %d\n", num, fd);
    for (int i = 0; i < num; i++) {
        printf("%c", kbf->buf_[i]);
    }
}

int main() {
    int read_fd = open(__FILE__, O_RDONLY);
    if (read_fd < 0) {
        perror("open file failed");
        return EXIT_FAILURE;
    }
    auto readtask = createTaskWithHandler(read_fd, example_read_handler);
    readtask->set_task_type(task::TaskType::READ);
    io.addTask(readtask);

    sleep(INTMAX_MAX);

    return 0;
}