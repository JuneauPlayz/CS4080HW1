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
    new_node->previous 
}


