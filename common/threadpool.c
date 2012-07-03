#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include "mutex.h"
#include "stack.h"
#include "thread.h"

#include "threadpool.h"

struct thread_pool_s
{
    bool    is_run;

    struct stack_s* msg_list;

    struct mutex_s* msg_lock;

    struct thread_s** work_threads;
    bool*    work_thread_state;

    struct mutex_s* lock;
    struct thread_cond_s* cv;

    thread_msg_fun_pt   callback;
    int work_thread_num;

    int thread_index;
    struct mutex_s* thread_index_lock;
};

struct thread_pool_s* thread_pool_new(thread_msg_fun_pt callback, int thread_num, int msg_num)
{
    struct thread_pool_s* ret = (struct thread_pool_s*)malloc(sizeof(*ret));
    ret->msg_list = stack_new(msg_num,sizeof(void*));
    ret->msg_lock = mutex_new();

    ret->lock = mutex_new();
    ret->cv = thread_cond_new();

    ret->callback = callback;
    ret->work_thread_num = thread_num;
    ret->work_threads = (struct thread_s**)malloc(sizeof(struct thread_s*)*ret->work_thread_num);
    memset(ret->work_threads, 0, sizeof(struct thread_s*)*ret->work_thread_num);

    ret->work_thread_state = (bool*)malloc(sizeof(bool)*ret->work_thread_num);
    memset(ret->work_thread_state, false, sizeof(bool)*ret->work_thread_num);
    
    ret->thread_index_lock = mutex_new();
    ret->thread_index = 0;

    ret->is_run = true;

    return ret;
}

void thread_pool_delete(struct thread_pool_s* self)
{
    thread_pool_stop(self);

    stack_delete(self->msg_list);
    self->msg_list = NULL;

    mutex_delete(self->msg_lock);
    self->msg_lock = NULL;

    free(self->work_threads);

    mutex_delete(self->lock);
    self->lock = NULL;
    
    thread_cond_delete(self->cv);
    self->cv = 0;

    mutex_delete(self->thread_index_lock);
    self->thread_index_lock = NULL;

    free(self);
    self = NULL;
}

// TODO::�̼߳���ʱֻ����һ����Ϣ

// TODO::����Ӧ��Ϊ�̳߳����һ����־,��ʾ�Ƿ�һ���̼߳���ʱ��ȡ��ǰstack������Ϣ���д���
// TODO::����ֻ����һ����Ϣ

static void thread_pool_proc_onemsg(struct thread_pool_s* self)
{
    thread_msg_fun_pt callback = self->callback;
    void** msg_ptr = 0;

    mutex_lock(self->msg_lock);
    msg_ptr = (void**)stack_pop(self->msg_list);
    mutex_unlock(self->msg_lock);

    if(NULL != msg_ptr)
    {
        (*callback)(self, *msg_ptr);
    }
}

static void thread_pool_work(void* arg)
{
    struct thread_pool_s* self = (struct thread_pool_s*)arg;
    struct stack_s* msg_list = self->msg_list;
    thread_msg_fun_pt callback = self->callback;
    int thread_index = 0;
    bool* thread_state = self->work_thread_state;

    mutex_lock(self->thread_index_lock);
    thread_index = self->thread_index++;
    mutex_unlock(self->thread_index_lock);

    while(true)
    {
        if(stack_top(msg_list) <= 0)
        {
            // ���û����Ϣ,�����ͬ���߼�

            mutex_lock(self->lock);

            if(!self->is_run)
            {
                mutex_unlock(self->lock);
                break;
            }
            
            // ����ȡ���ɹ����ٴ��ж��Ƿ�����Ϣ,�����Ȼû����Ϣ�Ž��еȴ���������
            // ������޶����ö���̹߳���
            if(stack_top(msg_list) <= 0)
            {
                thread_cond_wait(self->cv, self->lock);
            }

            mutex_unlock(self->lock);
        }

        thread_state[thread_index] = true;

        thread_pool_proc_onemsg(self);

        thread_state[thread_index] = false;
    }
}

void thread_pool_start(struct thread_pool_s* self)
{
    if(NULL != self)
    {
        int i = 0;

        for(; i < self->work_thread_num; ++i)
        {
            self->work_threads[i] = thread_new(thread_pool_work, self);
        }
    }
}

void thread_pool_stop(struct thread_pool_s* self)
{
    if(NULL != self)
    {
        int i = 0;
        self->is_run = false;

        thread_cond_signal(self->cv);

        i = 0;
        for(; i < self->work_thread_num; ++i)
        {
            if(NULL != self->work_threads[i])
            {
                thread_delete(self->work_threads[i]);
            }
        }

        memset(self->work_threads, 0, sizeof(struct thread_s*)*self->work_thread_num);
    }
}

// ����̳߳��Ƿ�æ(û�д�����������Ϣ)
static bool thread_pool_isbusy(struct thread_pool_s* self)
{
    bool isbusy = false;

    if(stack_top(self->msg_list) > 0)
    {
        isbusy = true;
    }
    else
    {
        int thread_num = self->work_thread_num;
        int i = 0;

        for(; i < thread_num; ++i)
        {
            if(self->work_thread_state[i])
            {
                isbusy = true;
                break;
            }
        }
    }

    return isbusy;
}

void thread_pool_wait(struct thread_pool_s* self)
{
    // ѭ���ȴ��̳߳�,ֱ���䴦�ڷ�æ״̬
    while(true)
    {
        if(!thread_pool_isbusy(self))
        {
            break;
        }

        thread_sleep(1);
    }
}

void thread_pool_pushmsg(struct thread_pool_s* self, void* data)
{
    // �����Ϣ,��������������

    if(self->is_run)
    {
        mutex_lock(self->msg_lock);
        stack_push(self->msg_list, &data);
        mutex_unlock(self->msg_lock);

        thread_cond_signal(self->cv);
    }
}
