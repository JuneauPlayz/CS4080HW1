#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Node {
    char *value;
    struct Node *previous;
    struct Node *next;
} Node;

char *copy_string(const char *source) {
    char *copy = malloc(strlen(source) + 1);

    if (copy == NULL) {
        fprintf(stderr, "Cannot allocate memory. \n");
        exit(EXIT_FAILURE);
    }

    strcpy(copy, source);
    return copy;
}

void insert(Node **head, const char *value) {
    Node *new_node = malloc(sizeof(Node));

    if (new_node == NULL) {
        fprintf(stderr, "Cannot allocate memory.\n");
        exit(EXIT_FAILURE);
    }

    new_node->value = copy_string(value);
    new_node->previous = NULL;
    new_node->next = NULL;

    if (*head == NULL) {
        *head = new_node;
        return;
    }

    Node *current = *head;

    while (current->next != NULL) {
        current = current->next;
    }

    current->next = new_node;
    new_node->previous = current;

}

Node *find(Node *head, const char *value) {
    Node *current = head;

    while (current != NULL) {
        if (strcmp(current->value, value) == 0) {
            return current;
        }

        current = current->next;
    }

    return NULL;
}

int delete_item(Node **head, const char *value) {
    Node *node = find(*head, value);

    if (node == NULL) {
        return 0;
    }

    if (node->previous != NULL) {
        node->previous->next = node->next;
    } else {
        *head = node->next;
    }

    if (node->next != NULL) {
        node->next->previous = node-> previous;
    }

    free(node->value);
    free(node);

    return 1;
}
