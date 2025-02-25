//
// Created by 洛琪希 on 25-2-13.
//
/*
* HTTP/1.1 206 Partial Content\r\n
Content-Range: bytes 0-10239/512000\r\n
Content-Length: 10240\r\n
Content-Type: application/octet-stream\r\n
\r\n
（文件数据...）
 */
#include "tcp_client.h"

#include <iostream>
#include <fstream>
tcp_client::tcp_client(const std::shared_ptr<boost::asio::io_context>& io_context,bool use_ssl,const std::shared_ptr<boost::asio::ssl::context>& context)
: resolver_(*io_context) , use_ssl_(use_ssl)
{
    if(use_ssl_)
    {
        ssl_socket_ = std::make_unique<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(*io_context,*context);//https初始化
    }
    else
    {
        socket_ = std::make_unique<boost::asio::ip::tcp::socket>(*io_context);  // HTTP 初始化
    }
}

void tcp_client::async_connect(const std::string &host, int port,
    std::function<void(boost::system::error_code)> callback)
{
    auto self = shared_from_this();
    resolver_.async_resolve(host,std::to_string(port)//async_resolve调用后会自动传递ec和endpoints给回调函数
        ,[self,callback](boost::system::error_code ec,boost::asio::ip::tcp::resolver::results_type endpoints){
        if(ec)
        {
            std::cerr << "Resolve error: " << ec.message() << std::endl; // 记录日志
            callback(ec);
            return;
        }

        if(!self->use_ssl_)
        {//普通socket指针要解引用，ssl_socket不要解引用 用lowest_layer获取最底层socket即可。
            boost::asio::async_connect(*(self->socket_),endpoints,[self,callback](boost::system::error_code ec,boost::asio::ip::tcp::endpoint endpoint) {
                if(ec)
                    {
                        std::cerr << "connect error" << ec.message() << std::endl;
                    }
                callback(ec);
            });
        }else
        {//使用ssl_socket连接  多了一个握手
            boost::asio::async_connect((self->ssl_socket_)->lowest_layer(),endpoints,[self,callback](boost::system::error_code ec,boost::asio::ip::tcp::endpoint endpoint) {
                if(ec)
                {
                    std::cerr << "connect error" << ec.message() << std::endl;
                    callback(ec);
                    return;
                }
                self->ssl_socket_->async_handshake(boost::asio::ssl::stream_base::client,
                        [self, callback](boost::system::error_code ec) {
                            callback(ec);  // 握手完成后执行回调
                        });
            });
        }
    });
}


void tcp_client::send_head_request(const std::string &request, std::function<void(const std::string &)> on_response)
{
    auto self = shared_from_this(); // 确保对象在异步调用期间不会被销毁
    if(use_ssl_)
    {
        do_send_head_request(*ssl_socket_,request,on_response,self);
    }else
    {
        do_send_head_request(*socket_,request,on_response,self);
    }
}

void tcp_client::read_head_response(const std::function<void(const std::string &)>& on_response)
{
    auto self = shared_from_this();
    if(use_ssl_)
    {
        do_read_head_response(*ssl_socket_,on_response,self);
    }else
    {
        do_read_head_response(*socket_,on_response,self);
    }
}

void tcp_client::send_get_request(const std::shared_ptr<DownloadTask>& task,int i,const std::string & output_path,const std::string &request,
    const std::function<void(void)>& on_response)//这里函数用于处理最后的文件数据
{
    //发送get请求后会得到返回体，返回体为string类型，回调函数去处理这个string
    auto self = shared_from_this();
    if(use_ssl_)
    {
        do_send_get_request(*ssl_socket_,task,i,output_path,request,on_response,self);
    }else
    {
        do_send_get_request(*socket_,task,i,output_path,request,on_response,self);
    }
}

void tcp_client::read_get_response(const std::shared_ptr<DownloadTask>& task,int i,const std::string & output_path,const std::function<void(void)>& on_response)
{
    auto self = shared_from_this();
    if(use_ssl_)
    {
        do_read_get_response(*ssl_socket_,task,i,output_path,on_response,self);
    }else
    {
        do_read_get_response(*socket_,task,i,output_path,on_response,self);
    }
}

void tcp_client::async_read_remain_data(std::function<void(void)> on_response,
    std::size_t remaining_bytes,std::shared_ptr<std::ofstream> file_stream)
{
    auto self = shared_from_this();
    if(use_ssl_)
    {
        do_async_read_remain_data(*ssl_socket_,on_response,remaining_bytes,file_stream,self);
    } else
    {
        do_async_read_remain_data(*socket_,on_response,remaining_bytes,file_stream,self);
    }
}

void tcp_client::cancel()
{
    boost::system::error_code ec;
    if (socket_) socket_->close(ec);
    if (ssl_socket_) ssl_socket_->lowest_layer().close(ec);
}

void tcp_client::async_write_file(std::shared_ptr<std::ofstream> file_stream,
    const std::shared_ptr<std::vector<char>> &buffer, size_t length)
{
    boost::asio::async_write(*file_stream, boost::asio::buffer(*buffer, length),
        [file_stream](boost::system::error_code ec, std::size_t /*length*/) {
        if (ec) {
            std::cerr << "Error writing to file: " << ec.message() << std::endl;
            file_stream->close();
            std::exit(EXIT_FAILURE);
        }
    });
}

template<typename StreamType>
    void tcp_client::do_send_head_request(StreamType & stream,const std::string& request,std::function<void(const std::string &)> on_response,std::shared_ptr<tcp_client> self)
{
    boost::asio::async_write(stream,boost::asio::buffer(request),[self,on_response](boost::system::error_code ec, std::size_t /*length*/) {
        if(!ec)
        {
            self->read_head_response(on_response);
        }else
        {
            std::exit(EXIT_FAILURE);
            on_response("");
        }
    });
}

template<typename StreamType>
void tcp_client::do_read_head_response(StreamType &stream, const std::function<void(const std::string &)> &on_response,
    std::shared_ptr<tcp_client> self)
{
    boost::asio::async_read_until(stream,response_buffer_,"\r\n\r\n",
            [self,on_response](boost::system::error_code ec, std::size_t length) {
                if(!ec)
                {
                    std::istream response_stream(&self->response_buffer_);
                    std::string response(length, '\0');
                    response_stream.read(&response[0], static_cast<std::streamsize>(length));
                    //将size_t转换为streamsize可能有超出风险，但此处只是head请求回复，不会超出
                    on_response(response); // 回调
                }else
                {
                    std::exit(EXIT_FAILURE);
                    on_response(""); // 读取失败
                }
            });
}

template<typename StreamType>
void tcp_client::do_send_get_request(StreamType &stream, const std::shared_ptr<DownloadTask> &task, int i,
    const std::string &output_path, const std::string &request, const std::function<void()> &on_response,
    std::shared_ptr<tcp_client> self)
{
    boost::asio::async_write(stream,boost::asio::buffer(request),
        [on_response,self,i,output_path,task](const boost::system::error_code &ec,size_t length) {
        if(!ec)
        {
            self->read_get_response(task,i,output_path,on_response);
        }else
        {
            std::exit(EXIT_FAILURE);
            return;
        }
    });
}

template<typename StreamType>
void tcp_client::do_read_get_response(StreamType &stream, const std::shared_ptr<DownloadTask> &task, int i,
    const std::string &output_path, const std::function<void()> &on_response,std::shared_ptr<tcp_client> self)
{
    constexpr  size_t buffer_size = 64 * 1024; // 64KB 缓冲区
    auto buffer = std::make_shared<std::vector<char>>(buffer_size);

    boost::asio::async_read_until(stream,self->response_buffer_,"\r\n\r\n",
        [on_response,self,i,output_path,task,buffer](boost::system::error_code ec,size_t length) {
        if(!ec)
        {
            std::istream response_stream(&self->response_buffer_);
            std::string header;
            std::getline(response_stream, header);
            header.erase(header.find_last_not_of('\r') + 1); // 移除 `\r`
            if (header.find("206 Partial Content") == std::string::npos) {
                // 不是分段下载的返回，错误处理
                return;
            }

            std::ostringstream file_data;
            file_data << response_stream.rdbuf();//将可能多读的数据提取
            std::string temp_file_name = output_path + ".temp_" + std::to_string(i);
            auto file_stream = std::make_shared<std::ofstream>(temp_file_name,std::ios::binary | std::ios::out | std::ios::app);
            if (!file_stream->is_open())
            {
                std::cerr << "Failed to open file for writing.\n";
                std::exit(EXIT_FAILURE);
                return;  // 或其他错误处理
            }
            file_stream->write(file_data.str().data(),static_cast<std::streamsize>(file_data.str().size()));//这里是多读部分，不会超过streamsize
            if (!file_stream->good()) {
                std::cerr << "File write error.\n";
                file_stream->close();
                std::exit(EXIT_FAILURE);
                return;
            }
            if (task->paused) {
                self->cancel();
                return;
            }
                {
                    std::lock_guard<std::mutex> lock(task->task_mtx);
                    // 确保该临时文件名称已保存到 task->temp_files 中
                    auto it = std::find(task->temp_files.begin(), task->temp_files.end(), temp_file_name);
                    if (it == task->temp_files.end()) {
                        task->temp_files.push_back(temp_file_name);
                    }
                }
            if(file_data.str().size() < task->ranges[i].second - task->ranges[i].first + 1)
            {
                size_t remain_size = (task->ranges[i].second - task->ranges[i].first + 1) - file_data.str().size();
                self->async_read_remain_data(on_response,remain_size,file_stream);
            }
        }
        else
        {
            std::exit(EXIT_FAILURE);
            on_response();
        }
    });
}

template<typename StreamType>
void tcp_client::do_async_read_remain_data(StreamType &stream, std::function<void()> on_response,
    std::size_t remaining_bytes, std::shared_ptr<std::ofstream> file_stream,std::shared_ptr<tcp_client> self)
{
    constexpr  size_t buffer_size = 64 * 1024;//64kb缓冲区
    auto buffer = std::make_shared<std::vector<char>>(buffer_size);
    std::size_t bytes_to_read = std::min<std::size_t>(buffer_size, remaining_bytes);

    stream.async_read_some(boost::asio::buffer(*buffer,bytes_to_read),
        [file_stream,buffer,self,remaining_bytes,on_response](boost::system::error_code ec,size_t length) {
        if(ec)
        {
            std::cerr << "Read error: " << ec.message() << std::endl;
            file_stream->close();
            std::exit(EXIT_FAILURE);
            return;
        }
        self->async_write_file(file_stream,buffer,static_cast<std::streamsize>(length));
        std::size_t new_remaining = remaining_bytes - length;
            if (new_remaining > 0) {
                // 继续读取剩余数据
                self->async_read_remain_data(on_response,new_remaining,file_stream);
            } else
            {
                // 所有数据读取完毕，关闭文件并调用完成回调
                file_stream->close();
                if(on_response)
                {
                    on_response();
                }
            }
    });
}
