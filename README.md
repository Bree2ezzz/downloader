# 基于boost.asio的http下载器

## 目前功能

维护下载队列，实现动态并发的多文件同时的多线程下载。

对https和http支持

暂停和续传逻辑正常

## 任务清单

`std::exit`为尚未完成的错误处理，当前若网络出错程序会直接退出

暂停暂时是直接传递task对象，会加入部分逻辑来优化。

## 待优化

目前多线程的应用感觉上意义暂无，如果在单线程中对文件进行分段然后多次的调用`async_read`也能实现分段下载，如果考虑到后续要加入超时处理，单个分段出错的重试，那么多线程的意义会更加大。

对于大文件和小文件应该分类，小文件使用多线程去下载带来的提升可能弥补不上线程开辟的消耗，获得文件大小后应该去判断是否要去使用多线程。

若传输的是流媒体数据，例如直播流需要播放的，需要解码。

目前下载器主要还是io密集型任务，只是对普通文件的下载，若要加入对流媒体数据的支持，则需要在传输中去解码。加入这样的支持那就是万能http下载器了，又能下载文件，又能边下边播视频，还能看直播。

![img](file:///C:\Users\洛琪希\AppData\Roaming\Tencent\QQTempSys\X4H~EV$MK34QZ4NM[5X%0TD.gif)
