#include "platform.h"

#ifdef PLATFORM_LINUX
#include <sys/epoll.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "stack.h"
#include "mutex.h"
#include "socketlibtypes.h"
#include "socketlibfunction.h"
#include "thread.h"
#include "threadpool.h"
#include "server_private.h"
#include "buffer.h"

#define EPOLL_WORKTHREAD_NUM (1)    /*  TODO:: �����û������߳������   */

#define MAX_EVENTS (50)

struct epollserver_s;

struct session_s
{
    struct epollserver_s*   epollserver;
    int index;
    sock fd;

    struct mutex_s*  send_mutex;    /*  TODO::���ǻ�Ϊ������    */
    struct buffer_s*    send_buffer;
    struct buffer_s*    recv_buffer;
};

struct epollserver_s
{
    struct server_s base;
    int epoll_fd;
    int listen_port;

    int session_recvbuffer_size;
    int session_sendbuffer_size;

    int max_num;
    struct session_s*   sessions;

    struct stack_s* freelist;
    int freelist_num;
    struct mutex_s* freelist_mutex;             /*  TODO::���ǻ�Ϊ������    */

    struct thread_s*    listen_thread;
    struct thread_s**  epoll_threads;           /*  epoll wait�߳���    */
    struct thread_pool_s*   recv_thread_pool;   /*  ��ȡ���ݵ��̳߳�(����epoll�߳��鷢�͵Ķ�ȡ��������)  */
};

static void epoll_handle_newclient(struct epollserver_s* epollserver, sock client_fd)
{
    if(epollserver->freelist_num > 0)
    {
        struct session_s*   session = NULL;

        mutex_lock(epollserver->freelist_mutex);

        {
            struct session_s** ppsession = (struct session_s**)stack_pop(epollserver->freelist);
            if(NULL != ppsession)
            {
                session = *ppsession;
                epollserver->freelist_num--;
            }
        }

        mutex_unlock(epollserver->freelist_mutex);

        if(NULL != session)
        {
            /*  TODO::���ǵ������̻߳��������ʱ,����EPOLL�߳���Ȼ���ܴ����ѹر�socket������    */
            /*  1:epoll��ȡ�¼�(����1fd)��׼���Ժ���; 2:�߼���close 1fd��Ӧ��session ; 3:�����߳�ʹ�ø�close��session��Ϊ������ */
            struct server_s* server = &epollserver->base;
            session->fd = client_fd;
            buffer_init(session->recv_buffer);
            buffer_init(session->send_buffer);
            socket_nonblock(client_fd);

            (*(server->logic_on_enter))(server, session->index);
        }
        else
        {
            printf("û�п�����Դ\n");
        }
    }
}

static void epollserver_handle_sessionclose(struct session_s* session)
{
    struct epollserver_s* epollserver = session->epollserver;

    mutex_lock(epollserver->freelist_mutex);

    if(SOCKET_ERROR != session->fd)
    {
#ifdef PLATFORM_LINUX
        epoll_ctl(epollserver->epoll_fd, EPOLL_CTL_DEL, session->fd, NULL);
#endif
        socket_close(session->fd);
        session->fd = SOCKET_ERROR; 
        stack_push(epollserver->freelist, &session);
        epollserver->freelist_num++;
    }

    mutex_unlock(epollserver->freelist_mutex);

}

/*  �����߳�,TODO::�����Ƿ���acceptǰ�ȴ��п�����Դ */
static void epoll_listen_thread(void* arg)
{
    struct epollserver_s* epollserver = (struct epollserver_s*)arg;
    
    sock client_fd = SOCKET_ERROR;
    struct sockaddr_in socketaddress;
    socklen_t size = sizeof(struct sockaddr);

    sock listen_fd = socket_listen(epollserver->listen_port, 25);

    for(;;)
    {
        while((client_fd = accept(listen_fd, (struct sockaddr*)&socketaddress, &size)) < 0)
        {
            if(EINTR == sErrno)
            {
                continue;
            }
        }

        if(SOCKET_ERROR != client_fd)
        {
            epoll_handle_newclient(epollserver, client_fd);
        }
    }
}

static int epollserver_senddata(struct session_s* session, const char* data, int len)
{
    int send_len = 0;
    int oldlen = 0;
    int oldsendlen = 0;
    struct buffer_s*    send_buffer = session->send_buffer;

    mutex_lock(session->send_mutex);

    oldlen = buffer_getreadvalidcount(send_buffer);

    if(oldlen > 0)  /*  ���ȷ������û�����δ���͵�����  */
    {
        oldsendlen = socket_send(session->fd, buffer_getreadptr(send_buffer), oldlen);
    }

    if(oldsendlen == oldlen && NULL != data)    /*  ���ʣ������ȫ���������,�ŷ����û���ָ�������� */
    {
        send_len = socket_send(session->fd, data, len);

        if(send_len >= 0 && send_len < len) /*  �������û��ʧ��,��û��ȫ������,��д�����û����� */
        {
            if(buffer_write(session->send_buffer, data+send_len, len-send_len))
            {
                send_len = len;
            }
        }
    }

    mutex_unlock(session->send_mutex);

    return send_len;
}

static void epoll_work_thread(void* arg)
{
    #ifdef PLATFORM_LINUX

    struct epollserver_s* epollserver = (struct epollserver_s*)arg;
    struct server_s* server = &epollserver->base;
    int nfds = 0;
    int i = 0;
    int epollfd = epollserver->epoll_fd;
    struct epoll_event events[MAX_EVENTS];
    uint32_t event_data = 0;

    for(;;)
    {
        nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);

        if(-1 == nfds)
        {
            break;
        }

        for(i = 0; i < nfds; ++i)
        {
            struct session_s*   session = (struct session_s*)(events[i].data.ptr);
            event_data = events[i].events;

            if(event_data & EPOLLRDHUP)
            {
                epollserver_handle_sessionclose(session);
                (*server->logic_on_close)(server, session->index);
            }
            else
            {
                if(events[i].events & EPOLLIN)
                {
                    thread_pool_pushmsg(epollserver->recv_thread_pool, session);
                }

                if(event_data & EPOLLOUT)
                {
                    epollserver_senddata(session, NULL, 0);
                }
            }
        }
    }

    #endif
}

/*  �̳߳���Ϣ������:��ȡ���� */
/*  TODO::��Ҫ�������ý��ջ���������,���¿���û��ȫ�����ܵ����(EPOLL��ETģʽ��ֻ֪ͨһ��) */
/*  TODO::�����ֿ������ж����?�����߼���̫��̫���� */

static void epoll_recvdata_callback(struct thread_pool_s* self, void* msg)
{
    struct session_s*   session = (struct session_s*)msg;
    struct buffer_s*    recv_buffer = session->recv_buffer;
    struct server_s*    server = &(session->epollserver->base);

    int can_recvlen = buffer_getwritevalidcount(recv_buffer);

    if(can_recvlen > 0)
    {
        int recv_len = recv(session->fd, buffer_getwriteptr(recv_buffer), can_recvlen, 0);
        bool is_close = false;

        if(0 == recv_len)
        {
            is_close = true;
        }
        else if(SOCKET_ERROR == recv_len)
        {
            is_close = (S_EWOULDBLOCK != sErrno);
        }
        else
        {
            buffer_addwritepos(recv_buffer, recv_len);

            {
                int proc_len = (*server->logic_on_recved)(server, session->index, buffer_getreadptr(recv_buffer), recv_len);
                buffer_addreadpos(recv_buffer, proc_len);
            }

            if(buffer_getwritevalidcount(recv_buffer) <= 0) /*  �������ȫ������β��,���ƶ���ͷ��    */
            {
                buffer_adjustto_head(recv_buffer);
            }
        }
        
        if(is_close)
        {
            epollserver_handle_sessionclose(session);
            (*server->logic_on_close)(server, session->index);
        }
    }
}

static void epollserver_start_callback(
    struct server_s* self,
    logic_on_enter_pt enter_pt,
    logic_on_close_pt close_pt,
    logic_on_recved_pt   recved_pt
    )
{
    struct epollserver_s* epollserver = (struct epollserver_s*)self;

    self->logic_on_enter = enter_pt;
    self->logic_on_close = close_pt;
    self->logic_on_recved = recved_pt;

    epollserver->freelist = stack_new(epollserver->max_num, sizeof(struct session_s*));
    epollserver->freelist_num = epollserver->max_num;
    epollserver->freelist_mutex = mutex_new();

    epollserver->sessions = (struct session_s*)malloc(sizeof(struct session_s)*epollserver->max_num);
    
    {
        int i = 0;
        for(; i < epollserver->max_num; ++i)
        {
            struct session_s* session = epollserver->sessions+i;
            session->index = i;
            session->epollserver = epollserver;
            session->fd = SOCKET_ERROR;

            session->send_mutex = mutex_new();
            session->send_buffer = buffer_new(epollserver->session_sendbuffer_size);
            session->recv_buffer = buffer_new(epollserver->session_recvbuffer_size);

            stack_push(epollserver->freelist, &session);
        }
    }

    epollserver->epoll_threads = (struct thread_s**)malloc(sizeof(struct thread_s*)*EPOLL_WORKTHREAD_NUM);

    {
        int i = 0;
        for(; i < EPOLL_WORKTHREAD_NUM; ++i)
        {
            epollserver->epoll_threads[i] = thread_new(epoll_work_thread, epollserver);
        }
    }

    epollserver->recv_thread_pool = thread_pool_new(epoll_recvdata_callback, EPOLL_WORKTHREAD_NUM, 1024);
    thread_pool_start(epollserver->recv_thread_pool);

    epollserver->listen_thread = thread_new(epoll_listen_thread, epollserver);
}

static void epollserver_stop_callback(struct server_s* self)
{
}

static void epollserver_closesession_callback(struct server_s* self, int index)
{
    struct epollserver_s* epollserver = (struct epollserver_s*)self;
    
    if(index >= 0 && index < epollserver->max_num)
    {
        struct session_s* session = epollserver->sessions+index;
        epollserver_handle_sessionclose(session);
    }
}

static int epollserver_send_callback(struct server_s* self, int index, const char* data, int len)
{
    struct epollserver_s* epollserver = (struct epollserver_s*)self;
    int send_len = -1;

    if(index >= 0 && index < epollserver->max_num)
    {
        struct session_s* session = epollserver->sessions+index;
        send_len = epollserver_senddata(session, data, len);
    }
    
    return send_len;
}

struct server_s* epollserver_create(
    int port, 
    int max_num,
    int session_recvbuffer_size,
    int session_sendbuffer_size)
{
    struct epollserver_s* epollserver = (struct epollserver_s*)malloc(sizeof(*epollserver));
    memset(epollserver, 0, sizeof(*epollserver));

    epollserver->sessions = NULL;
    epollserver->max_num = max_num;
    epollserver->listen_port = port;

    epollserver->base.start_pt = epollserver_start_callback;
    epollserver->base.stop_pt = epollserver_stop_callback;
    epollserver->base.closesession_pt = epollserver_closesession_callback;
    epollserver->base.send_pt = epollserver_send_callback;

    epollserver->session_recvbuffer_size = session_recvbuffer_size;
    epollserver->session_sendbuffer_size = session_sendbuffer_size;

#ifdef PLATFORM_LINUX
    epollserver->epoll_fd = epoll_create(1);
#endif

    return &epollserver->base;
}