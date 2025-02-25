//
// Created by 洛琪希 on 25-2-13.
//

#ifndef TCP_CLIENT_H
#define TCP_CLIENT_H

#include <boost/asio.hpp>
#include <memory>
#include "DownloadTask.h"

#include <boost/asio/ssl.hpp>
//该类封装tcp连接、数据读写
class tcp_client : std::enable_shared_from_this<tcp_client>{
public:
    explicit tcp_client(const std::shared_ptr<boost::asio::io_context>& io_context,bool use_ssl = false,const std::shared_ptr<boost::asio::ssl::context>& context = nullptr);
    void async_connect(const std::string& host, int port,
                      std::function<void(boost::system::error_code)> callback);
    void send_head_request(const std::string& request,std::function<void(const std::string &)> on_response);//发送head请求
    void read_head_response(const std::function<void(const std::string &)>& on_response);//解析head请求回复
    void send_get_request(const std::shared_ptr<DownloadTask>& task,int i,const std::string & output_path,const std::string& request,const std::function<void(void)>& on_response);
    void read_get_response(const std::shared_ptr<DownloadTask>& task,int i,const std::string & output_path,const std::function<void(void)>& on_response);//解析get回复
    void async_read_remain_data(std::function<void(void)> on_response,
         std::size_t remaining_bytes,std::shared_ptr<std::ofstream> file_stream);//读取剩余文件数据
    void cancel();

    void async_write_file(std::shared_ptr<std::ofstream> file_stream, const std::shared_ptr<std::vector<char>>& buffer, size_t length);


private:
    std::unique_ptr<boost::asio::ip::tcp::socket> socket_;  // http socket
    std::unique_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> ssl_socket_; // https socket

    boost::asio::streambuf response_buffer_;//
    boost::asio::ip::tcp::resolver resolver_;
    bool use_ssl_;

    template<typename StreamType>
    void do_send_head_request(StreamType & stream,const std::string& request,std::function<void(const std::string &)> on_response,std::shared_ptr<tcp_client> self);

    template<typename StreamType>
    void do_read_head_response(StreamType & stream,const std::function<void(const std::string &)>& on_response,std::shared_ptr<tcp_client> self);

    template<typename StreamType>
    void do_send_get_request(StreamType & stream,const std::shared_ptr<DownloadTask>& task,int i,const std::string & output_path,
        const std::string &request,const std::function<void(void)>& on_response,std::shared_ptr<tcp_client> self);

    template<typename StreamType>
    void do_read_get_response(StreamType & stream,const std::shared_ptr<DownloadTask>& task,int i,const std::string & output_path,const std::function<void(void)>& on_response,std::shared_ptr<tcp_client> self);

    template<typename StreamType>
    void do_async_read_remain_data(StreamType & stream,std::function<void(void)> on_response,
    std::size_t remaining_bytes,std::shared_ptr<std::ofstream> file_stream,std::shared_ptr<tcp_client> self);
};



#endif //TCP_CLIENT_H
