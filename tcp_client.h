//
// Created by 洛琪希 on 25-2-13.
//

#ifndef TCP_CLIENT_H
#define TCP_CLIENT_H

#include <boost/asio.hpp>
#include <memory>
#include "DownloadTask.h"
//该类封装tcp连接、数据读写
class tcp_client : std::enable_shared_from_this<tcp_client>{
public:
    explicit tcp_client(const std::shared_ptr<boost::asio::io_context>& io_context);
    void async_connect(const std::string& host, int port,
                      std::function<void(boost::system::error_code)> callback);
    void send_head_request(const std::string& request,std::function<void(const std::string &)> on_response);//发送head请求
    void read_head_response(const std::function<void(const std::string &)>& on_response);//解析head请求回复
    void send_get_request(const std::shared_ptr<DownloadTask>& task,int i,const std::string & output_path,const std::string& request,const std::function<void(void)>& on_response);
    void read_get_response(const std::shared_ptr<DownloadTask>& task,int i,const std::string & output_path,const std::function<void(void)>& on_response);//解析get回复
    void async_read_remain_data(std::function<void(void)> on_response,
         std::size_t remaining_bytes,std::shared_ptr<std::ofstream> file_stream);//读取剩余文件数据
    void cancel();


private:
    boost::asio::ip::tcp::socket socket_;  // TCP Socket
    boost::asio::streambuf response_buffer_;//
    boost::asio::ip::tcp::resolver resolver_;
};



#endif //TCP_CLIENT_H
