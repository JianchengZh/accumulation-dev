#ifndef _IOCP_H_INCLUDED
#define _IOCP_H_INCLUDED

#ifdef  __cplusplus
extern "C" {
#endif

struct server_s;

//  session_recvbuffer_size : ���ý��ջ�������С
//  session_sendbuffer_size : ���÷��ͻ�������С(���Ͷ��δ�ɹ�������)

struct server_s* iocp_create(
    int port, 
    int max_num,
    int session_recvbuffer_size,
    int session_sendbuffer_size
    );

void iocp_delete(struct server_s* self);

#ifdef  __cplusplus
}
#endif

#endif
