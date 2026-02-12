/*
 * NXP Intrusive Doubly-Linked List
 *
 * Zero-allocation linked list. Embed nxp_list_node in your struct.
 */
#ifndef NXP_INTRUSIVE_LIST_H
#define NXP_INTRUSIVE_LIST_H

#include <stddef.h>
#include <stdbool.h>

/* Container-of macro (C23 typeof) */
#define nxp_container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

typedef struct nxp_list_node {
    struct nxp_list_node *prev;
    struct nxp_list_node *next;
} nxp_list_node;

typedef struct nxp_list {
    nxp_list_node head;   /* Sentinel node */
    size_t        count;
} nxp_list;

/* Initialize list (must be called before use) */
static inline void nxp_list_init(nxp_list *list) {
    list->head.prev = &list->head;
    list->head.next = &list->head;
    list->count     = 0;
}

static inline bool nxp_list_is_empty(const nxp_list *list) {
    return list->head.next == &list->head;
}

static inline size_t nxp_list_count(const nxp_list *list) {
    return list->count;
}

/* Insert node after `after` */
static inline void nxp_list_insert_after(nxp_list *list, nxp_list_node *after, nxp_list_node *node) {
    node->prev = after;
    node->next = after->next;
    after->next->prev = node;
    after->next = node;
    list->count++;
}

/* Push to front */
static inline void nxp_list_push_front(nxp_list *list, nxp_list_node *node) {
    nxp_list_insert_after(list, &list->head, node);
}

/* Push to back */
static inline void nxp_list_push_back(nxp_list *list, nxp_list_node *node) {
    nxp_list_insert_after(list, list->head.prev, node);
}

/* Remove a node from the list */
static inline void nxp_list_remove(nxp_list *list, nxp_list_node *node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->prev = nullptr;
    node->next = nullptr;
    list->count--;
}

/* Pop from front (returns nullptr if empty) */
static inline nxp_list_node *nxp_list_pop_front(nxp_list *list) {
    if (nxp_list_is_empty(list)) return nullptr;
    nxp_list_node *node = list->head.next;
    nxp_list_remove(list, node);
    return node;
}

/* Iteration macros */
#define nxp_list_for_each(list, node) \
    for (nxp_list_node *(node) = (list)->head.next; \
         (node) != &(list)->head; \
         (node) = (node)->next)

#define nxp_list_for_each_safe(list, node, tmp) \
    for (nxp_list_node *(node) = (list)->head.next, *(tmp) = (node)->next; \
         (node) != &(list)->head; \
         (node) = (tmp), (tmp) = (node)->next)

#endif /* NXP_INTRUSIVE_LIST_H */
