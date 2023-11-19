#ifndef __LIBS_LIST_H__
#define __LIBS_LIST_H__

#ifndef __ASSEMBLER__

#include <defs.h>

/* *
 * Simple doubly linked list implementation. 简单的双向链表
 *
 * Some of the internal functions ("__xxx") are useful when manipulating
 * whole lists rather than single entries, as sometimes we already know
 * the next/prev entries and we can generate better code by using them
 * directly rather than using the generic single-entry routines.
 * */

//链表的节点
struct list_entry {
    struct list_entry *prev, *next;
    //包括前向和后向指针
};

typedef struct list_entry list_entry_t;

//初始化链表节点，使其成为一个独立的循环链表
static inline void list_init(list_entry_t *elm) __attribute__((always_inline));
//添加elm节点在listelm节点后面
static inline void list_add(list_entry_t *listelm, list_entry_t *elm) __attribute__((always_inline));
//将elm节点加到listelm节点之前
static inline void list_add_before(list_entry_t *listelm, list_entry_t *elm) __attribute__((always_inline));
//将elm节点加到listelm节点后面
static inline void list_add_after(list_entry_t *listelm, list_entry_t *elm) __attribute__((always_inline));
//从链表中删除listelm节点，list_empty()函数对于listelm节点会返回false
static inline void list_del(list_entry_t *listelm) __attribute__((always_inline));
//从链表中删除list_entry_t节点，并重新初始化，list_empty()函数会返回true
static inline void list_del_init(list_entry_t *listelm) __attribute__((always_inline));
//判断list链表是否为空，即该链表中是否只有这一个节点
static inline bool list_empty(list_entry_t *list) __attribute__((always_inline));
//得到listelm的下一个节点
static inline list_entry_t *list_next(list_entry_t *listelm) __attribute__((always_inline));
//得到listelm的前一个节点
static inline list_entry_t *list_prev(list_entry_t *listelm) __attribute__((always_inline));

//将elm插入到已知相邻节点之间。这是一种内部函数，用于直接操作已知相邻节点
static inline void __list_add(list_entry_t *elm, list_entry_t *prev, list_entry_t *next) __attribute__((always_inline));
//从链表中删除一个节点，通过将前一个结点和后一个节点连起来，也是内部函数
static inline void __list_del(list_entry_t *prev, list_entry_t *next) __attribute__((always_inline));

/* *
 * list_init - initialize a new entry
 * @elm:        new entry to be initialized
 * */
static inline void
list_init(list_entry_t *elm) {
    elm->prev = elm->next = elm;
}

/* *
 * list_add - add a new entry
 * @listelm:    list head to add after 在这个节点后添加
 * @elm:        new entry to be added 添加的节点
 *
 * Insert the new element @elm *after* the element @listelm which
 * is already in the list.
 * */
static inline void
list_add(list_entry_t *listelm, list_entry_t *elm) {
    list_add_after(listelm, elm);
}

/* *
 * list_add_before - add a new entry
 * @listelm:    list head to add before
 * @elm:        new entry to be added
 *
 * Insert the new element @elm *before* the element @listelm which
 * is already in the list.
 * */
static inline void
list_add_before(list_entry_t *listelm, list_entry_t *elm) {
    __list_add(elm, listelm->prev, listelm);
}

/* *
 * list_add_after - add a new entry
 * @listelm:    list head to add after
 * @elm:        new entry to be added
 *
 * Insert the new element @elm *after* the element @listelm which
 * is already in the list.
 * */
static inline void
list_add_after(list_entry_t *listelm, list_entry_t *elm) {
    __list_add(elm, listelm, listelm->next);
}

/* *
 * list_del - deletes entry from list
 * @listelm:    the element to delete from the list
 *
 * Note: list_empty() on @listelm does not return true after this, the entry is
 * in an undefined state.
 * */
static inline void
list_del(list_entry_t *listelm) {
    __list_del(listelm->prev, listelm->next);
}

/* *
 * list_del_init - deletes entry from list and reinitialize it.
 * @listelm:    the element to delete from the list.
 *
 * Note: list_empty() on @listelm returns true after this.
 * */
static inline void
list_del_init(list_entry_t *listelm) {
    list_del(listelm);
    list_init(listelm);
}

/* *
 * list_empty - tests whether a list is empty
 * @list:       the list to test.
 * */
static inline bool
list_empty(list_entry_t *list) {
    return list->next == list;
}

/* *
 * list_next - get the next entry
 * @listelm:    the list head
 **/
static inline list_entry_t *
list_next(list_entry_t *listelm) {
    return listelm->next;
}

/* *
 * list_prev - get the previous entry
 * @listelm:    the list head
 **/
static inline list_entry_t *
list_prev(list_entry_t *listelm) {
    return listelm->prev;
}

/* *
 * Insert a new entry between two known consecutive entries.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 * */
static inline void
__list_add(list_entry_t *elm, list_entry_t *prev, list_entry_t *next) {
    prev->next = next->prev = elm;
    elm->next = next;
    elm->prev = prev;
}

/* *
 * Delete a list entry by making the prev/next entries point to each other.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 * */
static inline void
__list_del(list_entry_t *prev, list_entry_t *next) {
    prev->next = next;
    next->prev = prev;
}

#endif /* !__ASSEMBLER__ */

#endif /* !__LIBS_LIST_H__ */

