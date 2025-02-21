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
    std::regex url_regex(R"(^(http|https)://([^;/]+)(?::(\d+))?(/.*)?$)");
    /*正则表达式
     * 从^开始  $结束。需要从url里解析出多条信息，用（）进行分组
     * 第一组（http|https） ://是url固定写法，不记录在分组里
     * 第二组([^:/]+) 匹配:或/前的一个或多个字符 第三组(?:   )这里代表非捕获分组，为了将后续的:和数字绑定在一起
     * 最外面的？代表这一部分可选 第四组代表从/开始所有的字符，且该部分可选
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
        auto segment_client = std::make_shared<tcp_client>(task->io_context);
        std::string get_request = build_get_request(task->path, task->host, range.first, range.second, downloaded);
        segment_client->async_connect(task->host,task->port,[this,segment_client,get_request,task,i,temp_file](boost::system::error_code ec) {
            if (ec) {
                handle_error(task);
                return;
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
                       if(task->active_segments == 0)
                       {
                           task->work_guard.reset();
                           //join线程和合并文件
                           for(auto & thread : task->threads)
                           {
                               thread.join();
                           }
                           std::thread merge_thread([this,task]() {
                               merge_file(task->output_path,task);
                               delete_temp_file(task);
                           });
                           merge_thread.detach();
                       }
            });
        });
    }
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

    auto file_io_context_ = std::make_shared<boost::asio::io_context>();//每个文件一个io_ctx，避免资源竞争，实现多文件不同线程数下载
    const auto work_guard = boost::asio::make_work_guard(*file_io_context_);

    std::vector<std::thread> file_threads;
    for (int i = 0; i < thread_num; ++i) {
        file_threads.emplace_back([file_io_context_]() {
            file_io_context_->run();
        });
    }
    auto task = std::make_shared<DownloadTask>(file_io_context_,work_guard);
    task->threads = std::move(file_threads);
    //保存信息方便续传
    task->output_path = output_path;
    task->host = host;
    task->path = path;
    task->port = port;


    auto client = std::make_shared<tcp_client>(file_io_context_);
    {
        std::lock_guard<std::mutex> lock(downloader_mtx);
        active_downloads_[task] = client;
    }

    client->async_connect(host,port,[client,this,task, host, path, port, output_path, thread_num](boost::system::error_code ec) {
        if(ec)
        {
            handle_error(client);
            return;
        }
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

            for(int i = 0; i < static_cast<int>(ranges.size()); ++i)
            {
                auto range = ranges[i];
                std::string get_request = build_get_request(path,host,range.first,range.second,0);
                auto segment_client = std::make_shared<tcp_client>(task->io_context);
                //连接
                segment_client->async_connect(host,task->port,[client,this,segment_client,get_request,output_path,range,i,task,thread_num](boost::system::error_code ec) {
                    if (ec) {
                        handle_error(client);
                        return;
                    }
                    ++task->active_segments;
                    segment_client->send_get_request(task,i,output_path,get_request,[output_path,range,this,i,task,thread_num]() {
                        {
                           std::lock_guard<std::mutex> lock(task->task_mtx);
                           task->temp_files.emplace_back(output_path + ".temp_" + std::to_string(i));
                        }
                        --task->active_segments;
                        if(task->active_segments == 0)
                        {
                            task->work_guard.reset();
                            //join线程和合并文件
                            for(auto & thread : task->threads)
                            {
                                thread.join();
                            }
                            std::thread merge_thread([output_path,this,task]() {
                                merge_file(output_path,task);
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
    for(const auto& filename : task->temp_files)
    {
        try
        {
            std::filesystem::remove(filename);//删除文件
            std::cout << "temp file" + filename << "delete success" << std::endl;
        }
        catch (const std::filesystem::filesystem_error & e)
        {
            std::cerr << "Error deleting file" << e.what() << std::endl;
        }
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
