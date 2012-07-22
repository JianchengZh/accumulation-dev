#ifndef _EPOLLSERVER_H_INCLUDED
#define _EPOLLSERVER_H_INCLUDED

#ifdef  __cplusplus
extern "C" {
#endif

    struct server_s;

    //  session_recvbuffer_size : ���ý��ջ�������С
    //  session_sendbuffer_size : ���÷��ͻ�������С(���Ͷ��δ�ɹ�������)

    struct server_s* epollserver_create(
        int port, 
        int max_num,
        int session_recvbuffer_size,
        int session_sendbuffer_size
        );

    void epollserver_delete(struct server_s* self);

#ifdef  __cplusplus
}
#endif

#endif
