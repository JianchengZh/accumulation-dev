#ifndef _EVENT_H_INCLUDED_
#define _EVENT_H_INCLUDED_

#define MAX_OBJECT_EVENT_NUM 20         // ����ӵ���¼�����
#define MAX_EVENT_OBSERVER_NUM    20    // ÿ���¼����ӵ�еĹ۲�������
#define MAX_EVENT_HANDLER_NUM 20        // ÿ���¼����ص�������

#ifdef  __cplusplus
extern "C" {
#endif

    struct object_s;
    struct event_handler_s;

    typedef void (*event_handler_pt)(struct object_s* root, struct object_s* source, void* data, void* arg);

    struct object_s* object_create();

    void object_insert_observer(struct object_s* root, int event_id, struct object_s* observer);
    void object_insert_handler(struct object_s* root, int event_id, event_handler_pt callback, void* event_data);

    void object_handler(struct object_s* root, struct object_s* source, int event_id, void* arg);
    void object_nofify(struct object_s* root, int event_id, void* arg);

#ifdef  __cplusplus
}
#endif


#endif
