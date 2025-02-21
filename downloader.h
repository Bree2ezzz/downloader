//
// Created by 洛琪希 on 25-2-13.
//

#ifndef DOWNLOADER_H
#define DOWNLOADER_H

#include <string>
#include <regex>
#include <boost/asio.hpp>
#include "tcp_client.h"
#include <set>
#include <queue>
#include <memory>
#include <utility>
#include <filesystem>
#include "DownloadTask.h"

class downloader {
public:
    explicit downloader(size_t max_concurrent);
    void add_download_task(const std::string &url, const std::string &output_path, int thread_num);//将下载任务加入队列

    void pause_download(const std::shared_ptr<DownloadTask>& task);
    void resume_download(const std::shared_ptr<DownloadTask>& task);

    //妙妙工具
    static bool compare(const std::string& a, const std::string& b) {
        // 提取数字部分并转换成整数
        int num_a = std::stoi(a.substr(a.find_last_of('_') + 1));
        int num_b = std::stoi(b.substr(b.find_last_of('_') + 1));
        return num_a < num_b;
    }

private:
    std::string build_head_request(const std::string &path,const std::string& host);//构造head请求
    static std::string build_get_request(const std::string &path,const std::string &host,size_t start, size_t end);
    static std::string build_get_request(const std::string &path, const std::string &host, size_t start, size_t end, size_t downloaded_offset);
    static bool parse_url(const std::string& url, std::string& host, std::string& path, int& port);//解析url
    size_t parse_head_response(const std::string& response);        //解析头响应文件，获得文件大小
    void start_next_download();
    std::vector<std::pair<size_t,size_t>> split_file_into_ranges(size_t file_size,int thread_num);

    void handle_error(std::shared_ptr<tcp_client> client);
    void merge_file(const std::string& output_path,std::shared_ptr<DownloadTask> task);
    auto delete_temp_file(const std::shared_ptr<DownloadTask>& task) -> void;


    size_t max_concurrent_downloads_;    //最大同时下载数
    std::queue<std::tuple<std::string, std::string, int>> download_queue_; // (url, output_path, thread_num)
    std::map<std::shared_ptr<DownloadTask>, std::shared_ptr<tcp_client>> active_downloads_;
    std::mutex downloader_mtx;//用于保护active_downloads。





};



#endif //DOWNLOADER_H
