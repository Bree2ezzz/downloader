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


    std::atomic<bool> paused{false};
    // 新增字段：保存每个分段的原始范围
    std::vector<std::pair<size_t, size_t>> ranges;
    // 保存下载任务相关的基本信息，便于续传时构造请求
    std::string output_path;
    std::string host;
    std::string path;
    int port;

    DownloadTask(std::shared_ptr<boost::asio::io_context> io_ctx,boost::asio::executor_work_guard<boost::asio::io_context::executor_type> wk)
        : io_context(std::move(io_ctx)) , work_guard(std::move(wk)){}
};
#endif //DOWNLOADTASK_H
