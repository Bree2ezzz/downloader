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
tcp_client::tcp_client(const std::shared_ptr<boost::asio::io_context>& io_context)
: socket_(*io_context) , resolver_(*io_context)
{
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
            boost::asio::async_connect(self->socket_,endpoints,
                [self,callback](boost::system::error_code ec,boost::asio::ip::tcp::endpoint endpoint) {
                if(ec)
                {
                    std::cerr << "connect error" << ec.message() << std::endl;
                }
                    callback(ec);
            });
    });
}


void tcp_client::send_head_request(const std::string &request, std::function<void(const std::string &)> on_response)
{
    auto self = shared_from_this(); // 确保对象在异步调用期间不会被销毁
    boost::asio::async_write(socket_, boost::asio::buffer(request),
        [self, on_response](boost::system::error_code ec,std::size_t /*length*/) {
            //异步写入和读取函数结束后都会向回调函数传递error_code和要发送/接收的字节数
            if (!ec) {
                self->read_head_response(on_response); // 发送成功后，异步读取响应
            } else {
                std::exit(EXIT_FAILURE);
                on_response(""); // 发生错误，返回空字符串
            }
        }
    );
}

void tcp_client::read_head_response(const std::function<void(const std::string &)>& on_response)
{
    auto self = shared_from_this();
    boost::asio::async_read_until(socket_,response_buffer_,"\r\n\r\n",
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

void tcp_client::send_get_request(const std::shared_ptr<DownloadTask>& task,int i,const std::string & output_path,const std::string &request,
    const std::function<void(void)>& on_response)//这里函数用于处理最后的文件数据
{
    //发送get请求后会得到返回体，返回体为string类型，回调函数去处理这个string
    auto self = shared_from_this();
    boost::asio::async_write(socket_,boost::asio::buffer(request),
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

void tcp_client::read_get_response(const std::shared_ptr<DownloadTask>& task,int i,const std::string & output_path,const std::function<void(void)>& on_response)
{
    auto self = shared_from_this();
    boost::asio::async_read_until(socket_,response_buffer_,"\r\n\r\n",
        [on_response,self,i,output_path,task](boost::system::error_code ec,size_t length) {
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
            std::size_t content_length = 0;
            while (std::getline(response_stream, header) && !header.empty())
            {
                // 移除末尾的 \r（如果有）
                if (!header.empty() && header.back() == '\r') {
                    header.pop_back();
                }

                // 查找 Content-Length
                size_t pos = header.find("Content-Length:");
                if (pos != std::string::npos) {
                    // 动态计算 Content-Length 值的起始位置
                    size_t start = pos + 16; // "Content-Length:" 的长度是 16
                    try {
                        content_length = std::stoul(header.substr(start));
                    } catch (const std::invalid_argument& e) {
                        std::cerr << "Error: Invalid Content-Length value!" << std::endl;
                    } catch (const std::out_of_range& e) {
                        std::cerr << "Error: Content-Length value out of range!" << std::endl;
                    }
                }
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
            if(file_data.str().size() < content_length)
            {
                size_t remain_size = content_length - file_data.str().size();
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

void tcp_client::async_read_remain_data(std::function<void(void)> on_response,
    std::size_t remaining_bytes,std::shared_ptr<std::ofstream> file_stream)
{

    auto self = shared_from_this();
    auto buffer = std::make_shared<std::vector<char>>(4096);
    std::size_t bytes_to_read = std::min<std::size_t>(buffer->size(), remaining_bytes);
    socket_.async_read_some(boost::asio::buffer(*buffer,bytes_to_read),
        [file_stream,buffer,self,remaining_bytes,on_response](boost::system::error_code ec,size_t length) {
        if(ec)
        {
            std::cerr << "Read error: " << ec.message() << std::endl;
            std::exit(EXIT_FAILURE);
            return;
        }
        file_stream->write(buffer->data(),static_cast<std::streamsize>(length));
        if (!file_stream->good())
        {
            std::cerr << "File write error." << std::endl;
            std::exit(EXIT_FAILURE);
            return;
        }
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

void tcp_client::cancel()
{
    boost::system::error_code ec;
    socket_.close(ec);
}

