#include <stdlib.h>
#include <stdio.h>
#include <string>

#include "platform.h"
#include "iocp.h"
#include "server.h"
#include "http_server.h"

// http serverÿ��session��socket�ײ���ջ�������С
const static int SOCKET_SEND_BUFFER_SIZE = 1000024;
// http server ���������
const static int MAX_SESSION_NUM = 24;

// http �����ļ�����
enum FILE_TYPE
{
    FILE_NONE,
    FILE_HTML,
    FILE_GIF,
    FILE_JPEG,
    FILE_MAX
};

struct http_session
{
    enum FILE_TYPE  filetype;
    const   char*   request;
    int request_len;

    char cmd[1024], arg[1024];

    int session_index;

    char* file_data;
    int file_size;
};

static server_s* http_server = NULL;
static http_session*    sessions = NULL;

// ���ӽ���ʱ�ص�����
static void my_logic_on_enter_pt(struct server_s* self, int index);
// ���ӶϿ�ʱ�ص�����
static void my_logic_on_close_pt(struct server_s* self, int index);
// ���յ�����ʱ�ص�����
static int my_logic_on_recved_pt(struct server_s* self, int index, const char* buffer, int len);

static void process_http(http_session* session);

void    http_server_init(int port)
{
    if(NULL == http_server)
    {
        // ����HTTP_PORT�˿�,���������ΪMAX_SESSION_NUM
        http_server = iocp_create(port, MAX_SESSION_NUM, 1024, SOCKET_SEND_BUFFER_SIZE);
        sessions = (http_session*)malloc(sizeof(http_session)*MAX_SESSION_NUM);
        memset(sessions, 0, sizeof(http_session)*MAX_SESSION_NUM);

        for(int i = 0; i < MAX_SESSION_NUM; ++i)
        {
            sessions[i].session_index = -1;
        }
    }
}

void    http_server_start()
{
    if(NULL != http_server)
    {
        server_start(http_server, my_logic_on_enter_pt, my_logic_on_close_pt, my_logic_on_recved_pt);
    }
}

void    http_server_stop()
{
    if(NULL != http_server)
    {
        server_stop(http_server);
        free(sessions);
        sessions = NULL;
    }
}

static void my_logic_on_enter_pt(struct server_s* self, int index)
{
    printf("%d���ӽ���\n", index);
    sessions[index].session_index = index;
}

static void my_logic_on_close_pt(struct server_s* self, int index)
{
    printf("%d���ӶϿ�\n",index);
    memset(sessions+index, 0, sizeof(sessions[index]));
    sessions[index].session_index = index;
}

static int my_logic_on_recved_pt(struct server_s* self, int index, const char* buffer, int len)
{
    // �Ƿ���������httpЭ��  (Ŀǰû����Ӱ�ȫ����,��ʱclose��������)
    if(strstr(buffer, "\r\n\r\n"))
    {
        http_session* session = sessions+index;
        session->request = buffer;
        session->request_len = len;

        if(session->session_index == index)
        {
            process_http(session);
        }

        return len;
    }

    return 0;
}

struct  
{
    enum FILE_TYPE  type;
    const char* name;       //  httpЭ�����ļ�������ַ���
    const char* http_name;  //  �������ͻ��˵�httpЭ���е��ַ���
}types[] = {
    {FILE_HTML, "html", "text/html"},
    {FILE_HTML, "htm", "text/html"},
    {FILE_GIF, "gif", "image/gif"},
    {FILE_JPEG, "jpg", "image/jpeg"},
    {FILE_JPEG, "jpeg", "image/jpeg"}
};

// ����session������ļ�����
static  void    file_type(http_session* session)
{
    const char* cp;
    if((cp = strrchr(session->arg, '.')))
    {
        cp += 1;

        for(int i = 0; i < (sizeof(types)/sizeof(types[0])); ++i)
        {
            if(strcmp(types[i].name, cp) == 0)
            {
                session->filetype = types[i].type;
                break;
            }
        }
    }
}

#define SOCKETSEND(S, DATA) (server_send(http_server, S->session_index, DATA, strlen(DATA)))

static void cannot_do(http_session* session)
{
    SOCKETSEND(session, "HTTP/1.0 501 Not Implemented\r\n");
    SOCKETSEND(session, "Content-type:text/plain\r\n");
    SOCKETSEND(session, "\r\n");
    SOCKETSEND(session, "That command is not yet implemented\r\n");
}

static void do_404(http_session* session)
{
    SOCKETSEND(session, "HTTP/1.0 404 Not Found\r\n");
    SOCKETSEND(session, "Content-type:text/plain\r\n");
    SOCKETSEND(session, "\r\n");
    SOCKETSEND(session, "The item you requested:");
    SOCKETSEND(session, session->arg);
    SOCKETSEND(session, "\r\nis not found\r\n");
}

void send_file(http_session* session)
{
    FILE* fp = fopen(session->arg, "rb");

    if(NULL == fp)
    {
        do_404(session);
    }
    else
    {
        fseek( fp, 0, SEEK_END );
        session->file_size = ftell( fp );
        fseek( fp, 0, SEEK_SET );
        session->file_data = (char*) malloc(session->file_size + 1);

        if(NULL != session->file_data)
        {
            fread(session->file_data, session->file_size, 1, fp);
            session->file_data[session->file_size] = 0;

            {
                SOCKETSEND(session, "HTTP/1.0 200 OK\r\n");
                SOCKETSEND(session, "Content-type:");
                SOCKETSEND(session, types[session->filetype].http_name);
                SOCKETSEND(session, "\r\n");
                SOCKETSEND(session, "\r\n");

                server_send(http_server, session->session_index, session->file_data, session->file_size);

                free(session->file_data);
                session->file_data = NULL;
            }
        }
        
        fclose( fp );
    }
}

static  void    do_cat(http_session* session)
{
    file_type(session);
    if(session->filetype != FILE_NONE)
    {
        send_file(session);
    }
    else
    {
        cannot_do(session);
    }
}

static  void    process_http(http_session* session)
{

#ifdef PLATFORM_WINDOWS
    strcpy((char*)session->arg, ".");
    if(sscanf(session->request, "%s%s", session->cmd, session->arg+1) != 2)
    {
        return;
    }
#else
    strcpy((char*)session->arg, "./");
    if(sscanf(session->request, "%s%s", session->cmd, session->arg+2) != 2)
    {
        return;
    }
#endif

    if(strcmp(session->cmd, "GET") == 0 || strcmp(session->cmd, "POST") == 0)
    {
        do_cat(session);
    }
    else
    {
        // error
    }

    // �������,�رտͻ�������
    server_close(http_server, session->session_index);
}