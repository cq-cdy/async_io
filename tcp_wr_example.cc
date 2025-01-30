#include <functional>
#include <iostream>

#include "async_io.hpp"

using namespace talon;
using namespace talon::task;

using namespace std;

// Create an IO handler object
talon::IOHandler io;

// Read handling function with write-back logic added
// This function will be called when the read operation is completed
void example_read_handler(KernelBufPtr kbf) {
    // Get the return value of the read operation
    auto ret = kbf->io_ret_v();
    // Get the file descriptor of the read operation
    auto fd = kbf->io_ret_fd();
    printf("in example_read_handler ret = %d,fd = %d\n", ret, fd);
    printf("read data:");
    for (int i = 0; i < ret; i++) {
        printf("%c", kbf->buf_[i]);
    }
    printf("\n");

    // Reverse the string
    // Convert the read data to a std::string type
    std::string original_str(kbf->buf_.begin(), kbf->buf_.end());
    std::string reversed_str = original_str;
    std::reverse(reversed_str.begin(), reversed_str.end());

    // Build the write-back description information
    // Build a write-back description string containing the file descriptor
    // information
    std::string write_desc =
        "peer fd = [" + std::to_string(fd) + "]: write back";
    // Concatenate the write-back description information and the reversed
    // string
    std::string write_str = write_desc + reversed_str;

    // Create a new KernelBuf and set the data
    KernelBufPtr write_buf = std::make_shared<KernelBuf>(write_str.size());
    std::copy(write_str.begin(), write_str.end(), write_buf->buf_.begin());

    // Create a write-back task
    auto writeTask = createTaskWithHandler(fd);
    writeTask->set_task_type(TaskType::WRITE);
    writeTask->set_buffer_hardly(write_buf);
    io.addTask(writeTask);
}

// Set the READ task to a long - connection in example_accpet_hanlder
void example_accpet_hanlder(KernelBufPtr kbf) {
    // Get the return value of the accept operation
    auto ret = kbf->io_ret_v();
    // Get the file descriptor of the accept operation
    auto fd = kbf->io_ret_fd();

    printf("New connection accepted on fd %d\n", ret);

    auto readTask = createTaskWithHandler(ret, example_read_handler);
    readTask->set_task_type(TaskType::READ);
    // Set the task to run indefinitely, representing a long - connection
    readTask->set_repeat_forever(true);
    io.addTask(readTask);
}

int main() {
    // if just local file io ,this op is not must.
    io.init_tcp_server(8081);
    // Create an accept task
    auto accpet_task =
        createTaskWithHandler(io.listen_fd(), example_accpet_hanlder);
    // Set the task to run indefinitely
    accpet_task->set_repeat_forever(true);
    // Set the task type to ACCEPT
    accpet_task->set_task_type(TaskType::ACCEPT);
    // Add the accept task to the IO handler
    io.addTask(accpet_task);
    // Cancel the accept task
    accpet_task->cancel(io.get_iouring());
    // Set the task to run indefinitely again
    accpet_task->set_repeat_forever(true);
    // Keep trying to add the accept task until it succeeds
    // addTask is thread-safety by atomic
    while (!io.addTask(accpet_task)) {
    }

    sleep(1000000);
    return 0;
}