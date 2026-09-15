/*
 * SgList reconstruction.
 *
 * The original implementation is the small C++ support class in
 * libSgm.a(SgList.o), compiled from SgList.c++.  IRIX exports the old
 * MIPSpro C++ mangled names below; defining them directly keeps that ABI
 * available on the host without depending on a modern C++ ABI.
 */

#include <stddef.h>
#include <stdlib.h>

typedef struct SgListNodeCompat SgListNodeCompat;
struct SgListNodeCompat
{
    void *object;
    SgListNodeCompat *prev;
    SgListNodeCompat *next;
};

typedef struct SgListCompat
{
    SgListNodeCompat *head;
    SgListNodeCompat *tail;
    SgListNodeCompat *currentNode;
    int count;
} SgListCompat;

enum { SGLIST_SUCCESS = 0, SGLIST_FAILURE = 1 };
enum { SGLIST_IFALSE = 0, SGLIST_ITRUE = 1 };

void *__nw__10SgListNodeSGUi(unsigned int size)
{
    return malloc(size ? size : 1U);
}

void __dl__10SgListNodeSGPv(void *obj)
{
    if (obj)
        free(obj);
}

void *__nw__6SgListSGUi(unsigned int size)
{
    return malloc(size ? size : 1U);
}

void __dl__6SgListSGPv(void *obj)
{
    if (obj)
        free(obj);
}

SgListCompat *__ct__6SgListGv(SgListCompat *list)
{
    if (!list)
        return NULL;

    list->head = NULL;
    list->tail = NULL;
    list->currentNode = NULL;
    list->count = 0;
    return list;
}

void Clear__6SgListGv(SgListCompat *list);

void __dt__6SgListGv(SgListCompat *list, int delete_flag)
{
    if (!list)
        return;

    Clear__6SgListGv(list);
    if (delete_flag & 1)
        __dl__6SgListSGPv(list);
}

void InitIterate__6SgListGv(SgListCompat *list)
{
    if (list)
        list->currentNode = NULL;
}

void *Next__6SgListGv(SgListCompat *list)
{
    SgListNodeCompat *node;

    if (!list)
        return NULL;

    node = list->currentNode;
    if (node)
        node = node->next;
    else
        node = list->head;

    list->currentNode = node;
    return node ? node->object : NULL;
}

void *Prev__6SgListGv(SgListCompat *list)
{
    SgListNodeCompat *node;

    if (!list)
        return NULL;

    node = list->currentNode;
    if (node)
        node = node->prev;
    else
        node = list->tail;

    list->currentNode = node;
    return node ? node->object : NULL;
}

int LocateFront__6SgListGv(SgListCompat *list)
{
    if (!list || !list->head)
        return SGLIST_FAILURE;

    list->currentNode = list->head;
    return SGLIST_SUCCESS;
}

int LocateEnd__6SgListGv(SgListCompat *list)
{
    if (!list || !list->tail)
        return SGLIST_FAILURE;

    list->currentNode = list->tail;
    return SGLIST_SUCCESS;
}

int Locate__6SgListGPv(SgListCompat *list, void *object)
{
    SgListNodeCompat *node;

    if (!list)
        return SGLIST_FAILURE;

    for (node = list->head; node; node = node->next)
    {
        if (node->object == object)
        {
            list->currentNode = node;
            return SGLIST_SUCCESS;
        }
    }

    return SGLIST_FAILURE;
}

int CurrentPosition__6SgListGv(SgListCompat *list)
{
    SgListNodeCompat *node;
    int position = 0;

    if (!list || !list->head)
        return 0;

    for (node = list->head; node; node = node->next)
    {
        ++position;
        if (node == list->currentNode)
            return position;
    }

    return 0;
}

/*
 * The original implementation is intentionally zero-based here even though
 * CurrentPosition() reports a one-based position.  Preserve that quirk.
 */
int LocatePosition__6SgListGi(SgListCompat *list, int position)
{
    SgListNodeCompat *node;
    int index = 0;

    if (!list)
        return SGLIST_FAILURE;

    for (node = list->head; node; node = node->next, ++index)
    {
        if (index == position)
        {
            list->currentNode = node;
            return SGLIST_SUCCESS;
        }
    }

    return SGLIST_FAILURE;
}

int IsInList__6SgListGPv(SgListCompat *list, void *object)
{
    SgListNodeCompat *node;

    if (!list)
        return SGLIST_IFALSE;

    for (node = list->head; node; node = node->next)
        if (node->object == object)
            return SGLIST_ITRUE;

    return SGLIST_IFALSE;
}

void *Retrieve__6SgListGv(SgListCompat *list)
{
    return list && list->currentNode ? list->currentNode->object : NULL;
}

void *First__6SgListGv(SgListCompat *list)
{
    return list && list->head ? list->head->object : NULL;
}

void *Last__6SgListGv(SgListCompat *list)
{
    return list && list->tail ? list->tail->object : NULL;
}

int Insert__6SgListGPv(SgListCompat *list, void *object)
{
    SgListNodeCompat *node;
    SgListNodeCompat *current;

    if (!list)
        return SGLIST_FAILURE;

    node = (SgListNodeCompat *)__nw__10SgListNodeSGUi(sizeof(*node));
    if (!node)
        return SGLIST_FAILURE;

    current = list->currentNode;
    if (!current && list->head)
    {
        /* libSgm.a(SgList.o) returns failure in this state. */
        __dl__10SgListNodeSGPv(node);
        return SGLIST_FAILURE;
    }

    node->object = object;

    if (!list->head)
    {
        node->prev = NULL;
        node->next = NULL;
        list->head = node;
        list->tail = node;
        list->currentNode = node;
    }
    else
    {
        node->prev = current->prev;
        node->next = current;

        if (node->prev)
            node->prev->next = node;
        current->prev = node;

        if (list->head == current)
            list->head = node;

        list->currentNode = node;
    }

    ++list->count;
    return SGLIST_SUCCESS;
}

int Append__6SgListGPv(SgListCompat *list, void *object)
{
    SgListNodeCompat *node;
    SgListNodeCompat *current;

    if (!list)
        return SGLIST_FAILURE;

    node = (SgListNodeCompat *)__nw__10SgListNodeSGUi(sizeof(*node));
    if (!node)
        return SGLIST_FAILURE;

    current = list->currentNode;
    if (!current && list->head)
    {
        __dl__10SgListNodeSGPv(node);
        return SGLIST_FAILURE;
    }

    node->object = object;

    if (!list->head)
    {
        node->prev = NULL;
        node->next = NULL;
        list->head = node;
        list->tail = node;
        list->currentNode = node;
    }
    else
    {
        node->next = current->next;
        if (node->next)
            node->next->prev = node;

        current->next = node;
        node->prev = current;

        if (list->tail == current)
        {
            list->tail = node;
            node->next = NULL;
        }

        list->currentNode = node;
    }

    ++list->count;
    return SGLIST_SUCCESS;
}

int Replace__6SgListGPv(SgListCompat *list, void *object)
{
    if (!list || !list->currentNode)
        return SGLIST_FAILURE;

    list->currentNode->object = object;
    return SGLIST_SUCCESS;
}

void *Remove__6SgListGv(SgListCompat *list)
{
    SgListNodeCompat *node;
    void *object;

    if (!list || !list->currentNode)
        return NULL;

    node = list->currentNode;
    object = node->object;

    if (node == list->head)
    {
        list->head = node->next;
        if (list->head)
            list->head->prev = NULL;
        list->currentNode = NULL;
    }
    else if (node == list->tail)
    {
        list->tail = node->prev;
        if (list->tail)
            list->tail->next = NULL;
        list->currentNode = list->tail;
    }
    else
    {
        node->prev->next = node->next;
        node->next->prev = node->prev;
        list->currentNode = node->prev;
    }

    --list->count;
    __dl__10SgListNodeSGPv(node);
    return object;
}

void Clear__6SgListGv(SgListCompat *list)
{
    SgListNodeCompat *node;

    if (!list)
        return;

    node = list->head;
    while (node)
    {
        SgListNodeCompat *next = node->next;
        __dl__10SgListNodeSGPv(node);
        node = next;
    }

    list->head = NULL;
    list->tail = NULL;
    list->currentNode = NULL;
    list->count = 0;
}
