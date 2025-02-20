//
// Created by 洛琪希 on 25-2-20.
//

#ifndef DOWNLOADTASK_H
#define DOWNLOADTASK_H
#include <memory>
#include <boost/asio/io_context.hpp>
struct DownloadTask {
    std::shared_ptr<boost::asio::io_context> io_context;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard;
    std::vector<std::thread> threads;
    std::atomic<int> active_segments{0};
    std::vector<std::string> temp_files;
    std::mutex task_mtx;//用于保护temp_files
    bool paused = false; //是否暂停
    std::vector<std::pair<int,size_t>> current_position;

    DownloadTask(std::shared_ptr<boost::asio::io_context> io_ctx,boost::asio::executor_work_guard<boost::asio::io_context::executor_type> wk)
        : io_context(std::move(io_ctx)) , work_guard(std::move(wk)){}
};
#endif //DOWNLOADTASK_H
