#include <fcntl.h>

#include <iostream>

#include "async_io.hpp"

using namespace talon;
using namespace talon::task;
using namespace std;

talon::IOHandler io;
// will be excute after if io success 
void example_read_handler(KernelBufPtr kbf) {
    auto num = kbf->io_ret_v();
    auto fd = kbf->io_ret_fd();
    printf("in example_read_handler ret = %d, fd = %d\n", num, fd);
    // foreach char data
    for (int i = 0; i < num; i++) {
        printf("%c", (*kbf)[i]);
    }
    // set next readTask continue read
    auto readtask = createTaskWithHandler(fd, example_read_handler);
    readtask->set_task_type(task::TaskType::READ);
    auto next_offset = kbf->get_fd_offset() + num; // set next task io offset, default 0;
    readtask->get_buffer()->set_fd_offset(next_offset);
    io.addTask(readtask);
}
int main() {
    int read_fd = open("./async_io.hpp", O_RDONLY);
    if (read_fd < 0) {
        perror("open file failed");
        return EXIT_FAILURE;
    }
    auto readtask = createTaskWithHandler(read_fd, example_read_handler);
    readtask->set_task_type(task::TaskType::READ);
    io.addTask(readtask);

    sleep(100000);
    return 0;
}