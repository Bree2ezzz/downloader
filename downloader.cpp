//
// Created by 洛琪希 on 25-2-13.
//

#include "downloader.h"

#include <utility>
#include <fstream>
#include <iostream>
#include <mutex>
#include <future>


downloader::downloader(size_t max_concurrent) : max_concurrent_downloads_(max_concurrent)
{
}



bool downloader::parse_url(const std::string &url, std::string &host, std::string &path, int &port)
{
    /*http协议
     * url传入应该是"http://example.com/large_file.zip
     * 使用正则表达式分割，从而获得host path和port。
     * http协议port未指定为默认80
     */
    // 正则表达式改进
    std::regex url_regex(R"(^(http|https)://([^:/?#]+)(?::(\d+))?(/[^?#]*)?$)");
    /*
     * 正则表达式解释：
     * ^(http|https):// 匹配协议部分（http 或 https）
     * ([^:/?#]+) 匹配主机部分，直到遇到 :、/、? 或 # 为止
     * (?::(\d+))? 匹配可选的端口部分
     * (/[^?#]*)? 匹配路径部分，直到遇到 ? 或 # 为止
     */
    std::smatch match;
    if(!std::regex_match(url,match,url_regex))
    {
        return false;//url格式错a误
    }
    std::string protocol = match[1].str();
    host = match[2].str();
    if(match[3].matched)
    {
        port = std::stoi(match[3].str());//stoi将字符串内的数字转换为int
    }else
    {
        port = (protocol == "https") ? 443 : 80;
    }
    path = match[4].matched ? match[4].str() : "/";

    return true;
}

void downloader::handle_error(std::shared_ptr<DownloadTask> task)
{
    {
        spdlog::warn("handle_error task tiggered");
        std::lock_guard<std::mutex> lock(downloader_mtx);
        active_downloads_.erase(task);
    }
    start_next_download();
}


void downloader::add_download_task(const std::string &url, const std::string &output_path, int thread_num)
{
    {
        std::lock_guard<std::mutex> lock(downloader_mtx);
        download_queue_.emplace(url, output_path, thread_num);
    }
    std::cout << "kaishile" <<std::endl;
    start_next_download();
}

void downloader::pause_download(const std::shared_ptr<DownloadTask> &task)
{
    std::lock_guard<std::mutex> lock(downloader_mtx);
    task->active_segments = 0;
    auto it = active_downloads_.find(task);
    if (it != active_downloads_.end()) {
        task->paused = true;
        it->second->cancel();
    }
}

void downloader::resume_download(const std::shared_ptr<DownloadTask> &task)
{
    std::lock_guard<std::mutex> lock(downloader_mtx);
    auto it = active_downloads_.find(task);
    if(it != active_downloads_.end())
    {
        task->paused = true;
    }
    for(auto & thread : task->threads)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));//暂停1s 防止刚暂停就继续，线程未结束
        if(thread.joinable())
        {
            std::cerr << "super big error. please submit issue";
            std::exit(EXIT_FAILURE);
        }
    }
    task->threads.clear();
    size_t thread_nums = task->ranges.size();
    for(size_t i = 0; i < thread_nums; ++i)
    {
        task->threads.emplace_back([task]() {
            task->io_context->run();
        });
    }
    for(size_t i = 0; i < thread_nums; ++i)
    {
        auto range = task->ranges[i];
        std::string temp_file = task->temp_files[i] + ".temp_" + std::to_string(i);
        size_t downloaded = 0;
        if (std::filesystem::exists(temp_file)) {
            downloaded = std::filesystem::file_size(temp_file);
        }
        if(downloaded >= range.second - range.first + 1)
        {//该分段已下完，跳过
            continue;
        }
        auto segment_client = std::make_shared<tcp_client>(task->io_context,task->use_ssl,task->ssl_context_);
        std::string get_request = build_get_request(task->path, task->host, range.first, range.second, downloaded);
        segment_client->async_connect(task->host,task->port,[this,segment_client,get_request,task,i,temp_file](boost::system::error_code ec) {
            if (ec) {
                handle_error(task);
                // return;
            }
            ++task->active_segments;
            segment_client->send_get_request(task,static_cast<int>(i),task->output_path,get_request,[task,this,temp_file]() {
                {
                    std::lock_guard<std::mutex> lock(task->task_mtx);
                    auto it = std::find(task->temp_files.begin(), task->temp_files.end(), temp_file);
                    if (it == task->temp_files.end()) {
                        task->temp_files.push_back(temp_file);
                    }
                }
                --task->active_segments;
                if (task->active_segments == 0)
                {
                    task->work_guard.reset();
                    //join线程和合并文件
                    for (auto &thread: task->threads)
                    {
                        thread.join();
                    }
                    std::thread merge_thread([this,task]() {
                        merge_file(task->output_path, task);
                        delete_temp_file(task);
                    });
                    merge_thread.detach();
                }
            });
        });
    }
    //这里是for循环结束
    {
        std::lock_guard<std::mutex> lock(downloader_mtx);
        active_downloads_.erase(task);
    }
    start_next_download();
}


void downloader::start_next_download()
{
    {
        std::lock_guard<std::mutex> lock(downloader_mtx);
        if(active_downloads_.size() >= max_concurrent_downloads_) return;//达到最大并发下载数
    }
    if(download_queue_.empty()) return; //下载队列为空

    // std::shared_ptr<DownloadTask> task;
    auto [url,output_path,thread_num] = download_queue_.front();
    download_queue_.pop();

    std::string host, path;
    int port;
    if (!parse_url(url, host, path, port)) return; // 解析失败
    spdlog::info("the program starts a download task");

    auto file_io_context_ = std::make_shared<boost::asio::io_context>();//每个文件一个io_ctx，避免资源竞争，实现多文件不同线程数下载
    const auto work_guard = boost::asio::make_work_guard(*file_io_context_);

    std::vector<std::thread> file_threads;
    for (int i = 0; i < thread_num; ++i) {
        file_threads.emplace_back([file_io_context_]() {
            file_io_context_->run();
        });
    }
    auto task = std::make_shared<DownloadTask>(file_io_context_,work_guard);

    // 确保 file_threads 里每个线程都是 joinable
    spdlog::info("file_threads size before move: {}", file_threads.size());
    for (size_t i = 0; i < file_threads.size(); ++i) {
        spdlog::info("file_threads[{}] joinable: {}", i, file_threads[i].joinable());
    }

    // 逐个 move 到 task->threads，防止丢失线程
    task->threads.clear();
    for (auto& thread : file_threads) {
        task->threads.push_back(std::move(thread));
    }

    // 确保 task->threads 里每个线程都是 joinable
    spdlog::info("task->threads size after move: {}", task->threads.size());
    for (size_t i = 0; i < task->threads.size(); ++i) {
        spdlog::info("task->threads[{}] joinable: {}", i, task->threads[i].joinable());
    }

    //保存信息方便续传
    task->output_path = output_path;
    task->host = host;
    task->path = path;
    task->port = port;
    if(port == 443)
    {
        task->use_ssl = true;
        task->ssl_context_ = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12_client);
        task->ssl_context_->set_verify_mode(boost::asio::ssl::verify_peer);
        task->ssl_context_->load_verify_file("path/to/cert.pem"); // 加载证书
    }


    auto client = std::make_shared<tcp_client>(file_io_context_,task->use_ssl,task->ssl_context_);
    {
        std::lock_guard<std::mutex> lock(downloader_mtx);
        active_downloads_[task] = client;
    }
    spdlog::info("the client of connect start the connection");
    client->async_connect(host,port,[client,this,task, host, path, port, output_path, thread_num](boost::system::error_code ec) {
        if(ec)
        {
            handle_error(client);
            // return;
        }
        spdlog::info("connect success");
        std::string head_request = build_head_request(path, host);//构造head请求
        /*发送head请求后的处理逻辑
 * 构造head请求后，调用send_request函数发送head请求，两个参数为请求文本、接受string变量返回void的函数func
 *在send_request的回调函数中执行read_response，func通过参数从send传递给read。read执行完后，read的回调
 *函数会执行func。
 */
        client->send_head_request(head_request,[this,client,thread_num,host,path,port,output_path,task](const std::string &response) {
            size_t file_size = parse_head_response(response);
            if (file_size == 0)
            {
                {
                    std::lock_guard<std::mutex> lock(downloader_mtx);
                    active_downloads_.erase(task);
                }
                return; // 获取大小失败
            }
            auto ranges = split_file_into_ranges(file_size,thread_num);
            task->ranges = ranges;
            spdlog::info("Header response parsing ends and segmentation ends");
            for(int i = 0; i < static_cast<int>(ranges.size()); ++i)
            {
                auto range = ranges[i];
                std::string get_request = build_get_request(path,host,range.first,range.second,0);
                auto segment_client = std::make_shared<tcp_client>(task->io_context,task->use_ssl,task->ssl_context_);
                //连接
                segment_client->async_connect(host,task->port,[segment_client,client,this,get_request,output_path,range,i,task,thread_num](boost::system::error_code ec) {
                    if (ec) {
                        handle_error(client);
                        // return;
                    }
                    spdlog::info(i + "Thread connection is successful");
                    ++task->active_segments;
                    segment_client->send_get_request(task,i,output_path,get_request,[segment_client,output_path,range,this,i,task,thread_num]() {
                        spdlog::info("{} Thread Complete the task", i);
                        --task->active_segments;
                        segment_client->cancel();
                        if(task->active_segments == 0)
                        {
                            spdlog::info("All segments finished, notifying main thread.");
                            // task->io_context.post([this, task]() { check_and_finalize_download(task); });
                            boost::asio::post(*(task->io_context), [task, this]() {
                                check_and_finalize_download(task);
                            });

                        }
                    });
                });

            }
        });

    });
}

std::vector<std::pair<size_t,size_t>> downloader::split_file_into_ranges(size_t file_size, int thread_num)
{
    std::vector<std::pair<size_t,size_t>> ranges;
    size_t chunk_size = file_size/thread_num;
    size_t remainder = file_size%thread_num;
    size_t start = 0;
    for(int i = 0; i < thread_num; ++i)
    {
        size_t end = start + chunk_size -1;
        if(i < remainder)
        {
            end++;
        }
        ranges.emplace_back(start,end);
        start = end + 1;
    }
    return ranges;
}

std::string downloader::build_get_request(const std::string &path, const std::string &host,size_t start, size_t end)
{
    return build_get_request(path,host,start,end,0);
}

std::string downloader::build_get_request(const std::string &path, const std::string &host, size_t start, size_t end,
    size_t downloaded_offset)
{
    std::string request = "GET " + path + " HTTP/1.1\r\n"
                          "Host: " + host + "\r\n"
                          "Range: bytes=" + std::to_string(start + downloaded_offset) + "-" + std::to_string(end) + "\r\n"
                          "Connection: close\r\n"
                          "\r\n";
    return request;
}

void downloader::handle_error(std::shared_ptr<tcp_client> client)
{
    std::lock_guard<std::mutex> lock(downloader_mtx);
    for(auto it = active_downloads_.begin();it != active_downloads_.end(); ++it)
    {
        if(it->second == client)
        {
            std::cout << "handle_error 触发" << std::endl;
            active_downloads_.erase(it);
            break;
        }
    }
    start_next_download();
}


void downloader::merge_file(const std::string &output_path, std::shared_ptr<DownloadTask> task)
{
    std::ofstream out_stream;
    out_stream.open(output_path,std::ios::out | std::ios::binary);
    if(!out_stream.is_open())
    {
        std::cerr << "merge_file_error" << std::endl;
        return;
    }
    std::sort(task->temp_files.begin(),task->temp_files.end(),compare);
    for(const auto& temp_file : task->temp_files)
    {
        if (!std::filesystem::exists(temp_file)) {
            spdlog::error("Temp file not found: {}", temp_file);
            continue;
        }
        std::ifstream file_data(temp_file,std::ios::in | std::ios::binary);
        if(!file_data.is_open())
        {
            std::cerr << "error open temp file" << std::endl;
            return;
        }
        out_stream << file_data.rdbuf();
    }
    out_stream.close();
    std::cout << "File merged successfully: " << output_path << std::endl;

}

void downloader::delete_temp_file(const std::shared_ptr<DownloadTask> & task)
{
    std::lock_guard<std::mutex> lock(task->task_mtx); // 确保线程安全
    for (const auto& filename : task->temp_files) {
        if (std::filesystem::exists(filename)) {
            try {
                std::filesystem::remove(filename);
                spdlog::info("Deleted temp file: {}", filename);
            } catch (const std::filesystem::filesystem_error& e) {
                spdlog::error("Error deleting {}: {}", filename, e.what());
            }
        }
    }
    task->temp_files.clear(); // 清空列表，防止重复删除
}

void downloader::check_and_finalize_download(std::shared_ptr<DownloadTask> task)
{
    std::lock_guard<std::mutex> lock(downloader_mtx);

    if (task->active_segments == 0 && !task->merged) {
        task->merged = true;
        spdlog::info("All segments finished. Stopping io_context...");

        // **确保 io_context 停止**
        spdlog::info("Resetting work_guard...");
        task->work_guard.reset();
        spdlog::info("work_guard reset complete. Calling io_context.stop()...");

        task->io_context->stop();
        spdlog::info("io_context.stop() called. Now joining threads...");

        std::vector<std::thread> threads_to_join;
        for (auto& thread : task->threads) {
            if (thread.joinable()) {
                threads_to_join.push_back(std::move(thread));
            }
        }
        task->threads.clear();

        // 在独立线程中执行 join() 和文件合并
        std::thread finalizer([threads_to_join = std::move(threads_to_join), this, task]() mutable {
            // 安全地 join 所有线程
            for (auto& thread : threads_to_join) {
                if (thread.joinable()) {
                    try {
                        thread.join();
                    } catch (const std::system_error& e) {
                        spdlog::error("Thread join failed: {}", e.what());
                    }
                }
            }
            spdlog::info("All segments end and merge begins");

        // **在独立线程中启动 `merge_file()`
            merge_file(task->output_path, task);
            delete_temp_file(task);
            {
                std::lock_guard<std::mutex> lock(downloader_mtx);
                active_downloads_.erase(task);
                start_next_download();
            }
        });
        finalizer.detach(); // 分离线程，避免阻塞
    }

}

size_t downloader::parse_head_response(const std::string& response)
{
    std::regex content_length_regex(R"(Content-Length:\s*(\d+))",std::regex::icase);
    std::smatch sm;
    if(std::regex_search(response,sm,content_length_regex))
    {
        return std::stoull(sm[1].str());//将string转换为size_t
    }else
    {
        return 0;
    }
}

std::string downloader::build_head_request(const std::string &path,const std::string &host)
{
    std::string request = "HEAD " + path + " HTTP/1.1\r\n"
            "Host: " + host + "\r\n"
            "Connection: close\r\n"
            "\r\n";
    return request;
}
