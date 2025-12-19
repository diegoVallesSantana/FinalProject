#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "dplist.h"

struct dplist_node {
    dplist_node_t *prev, *next;
    void *element;
};

struct dplist {
    dplist_node_t *head;
    int sizeOfList;
    void *(*element_copy)(void *src_element);
    void (*element_free)(void **element);
    int (*element_compare)(void *x, void *y);
};

dplist_t *dpl_create(// callback functions
        void *(*element_copy)(void *src_element),
        void (*element_free)(void **element),
        int (*element_compare)(void *x, void *y)
) {
    dplist_t *list;
    list = malloc(sizeof(struct dplist));
    list->head = NULL;
    list->element_copy = element_copy;
    list->element_free = element_free;
    list->element_compare = element_compare;
    list->sizeOfList=0;
    return list;
}

void dpl_free(dplist_t **list, bool free_element) {
        if (list == NULL) return;
        if (*list == NULL) return;

        dplist_node_t *dummy = (*list)->head;
        dplist_node_t *nextNode = NULL;

        while (dummy != NULL) { // not (dummy->next != NULL) bc this would exit before setting the last node free
            nextNode = dummy->next;
            if (free_element && (*list)->element_free != NULL) {
                (*list)->element_free(&(dummy->element));}
            free(dummy);
            dummy = nextNode;
        }

        free(*list);
        *list = NULL;
}

dplist_t *dpl_insert_at_index(dplist_t *list, void *element, int index, bool insert_copy) {
        dplist_node_t *ref_at_index, *list_node;
        if (list == NULL) return NULL;

        list_node = malloc(sizeof(dplist_node_t));
        if (list_node == NULL) return NULL;

        if ( insert_copy == 1){ list_node->element = list->element_copy(element);}
        else{list_node->element = element;}

        if (list->head == NULL) {
            list_node->prev = NULL;
            list_node->next = NULL;
            list->head = list_node;
            list->sizeOfList += 1;
        }
        else if (index <= 0) {
            list_node->prev = NULL;
            list_node->next = list->head;
            list->head->prev = list_node;
            list->head = list_node;
            list->sizeOfList += 1;
        }
        else {
            ref_at_index = dpl_get_reference_at_index(list, index);
            assert(ref_at_index != NULL);

            if (index < dpl_size(list)) {
                list_node->prev = ref_at_index->prev;
                list_node->next = ref_at_index;
                ref_at_index->prev->next = list_node;
                ref_at_index->prev = list_node;
                list->sizeOfList += 1;

            } else {
                list_node->next = NULL;
                list_node->prev = ref_at_index;
                ref_at_index->next = list_node;
                list->sizeOfList += 1;

            }
        }
        return list;
}

dplist_t *dpl_remove_at_index(dplist_t *list, int index, bool free_element) {

    if (list == NULL) return NULL;
    if (list->sizeOfList ==0) return list;

    dplist_node_t *dummy = list->head;

    if (index <= 0) {
        list->head = dummy->next;
        if(list->head != NULL){
            list->head->prev = NULL;
        }
        if (free_element && list->element_free != NULL) {
            list->element_free(&(dummy->element));
        }
        free(dummy);
        list->sizeOfList--;
        return list;
    }
    int counter = 0;
    while (dummy->next != NULL && counter < index) {
        dummy = dummy->next;
        counter++;
    }

    if (dummy->prev != NULL) {
        dummy->prev->next = dummy->next;
    }
    if (dummy->next != NULL) {
        dummy->next->prev = dummy->prev;
    }

    if (free_element && list->element_free != NULL) {
        list->element_free(&(dummy->element));
    }
    free(dummy);
    list->sizeOfList--;
    return list;
}

int dpl_size(dplist_t *list) {
    if (list == NULL){
        return -1;
    }
    return list->sizeOfList;
}

void *dpl_get_element_at_index(dplist_t *list, int index) {

    if (list == NULL) return NULL;
    if (list->sizeOfList ==0) return NULL;

    dplist_node_t *dummy = list->head;
    if (index <= 0) {
        return list->head->element;
    }
    int counter = 0;
    while (dummy->next != NULL && counter < index) {
        dummy = dummy->next;
        counter++;
    }
    return dummy->element;

}

int dpl_get_index_of_element(dplist_t *list, void *element) {

    if (list == NULL) return -1;
    if (list->sizeOfList ==0) return -1;
    if (list->head == NULL) return -1;

    dplist_node_t *dummy = list->head;
    int index = 0;

    while (dummy != NULL) {

        if ( list->element_compare(dummy->element,element) ==0){return index;}
        dummy = dummy->next;
        index++;
    }
    return -1;

}

dplist_node_t *dpl_get_reference_at_index(dplist_t *list, int index) {

    int count = 0 ;
    dplist_node_t *dummy = NULL;
    if (list == NULL) {return NULL;}
    if (list->sizeOfList==0) {return NULL;}

    if (index <=0) {
        dummy = list->head;
        return dummy;
    }
    dummy = list->head;
    while (dummy->next != NULL && count < index) {
        dummy = dummy->next;
        count++;
    }
    return dummy;
}


/** Returns the element contained in the list node with reference 'reference' in the list.
 * - If the list is empty, NULL is returned.
 * - If 'list' is is NULL, NULL is returned.
 * - If 'reference' is NULL, NULL is returned.
 * - If 'reference' is not an existing reference in the list, NULL is returned.
 * \param list a pointer to the list
 * \param reference a pointer to a certain node in the list
 * \return the element contained in the list node or NULL
 */
void *dpl_get_element_at_reference(dplist_t *list, dplist_node_t *reference) {
    if (list == NULL){return NULL;}
    if (list->sizeOfList ==0){return NULL;}
    if (reference == NULL){return NULL;}

    dplist_node_t *dummy = list->head;

    while (dummy != NULL) {
        if ( dummy == reference) {return dummy->element;}
        dummy = dummy->next;
    }
    return NULL;
}


