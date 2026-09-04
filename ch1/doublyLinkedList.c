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
        node->next->previous = node->previous;
    }

    free(node->value);
    free(node);

    return 1;
}

int main(void) {
    Node *head = NULL;

    printf("Inserting item1, item2, and item3.\n");
    insert(&head, "item1");
    insert(&head, "item2");
    insert(&head, "item3");

    Node *found = find(head, "item2");

    if (found != NULL) {
        printf("Found: %s\n", found->value);
        printf("Previous: %s\n", found->previous->value);
        printf("Next: %s\n", found->next->value);
    } else {
        printf("item2 not found.\n");
    }

    found = find(head, "item4");

    if (found != NULL) {
        printf("Found: %s\n", found->value);
    } else {
        printf("Item4 not found.\n");
    }

    if (delete_item(&head, "item2")) {
        printf("Deleted item2.\n");
    } else {
        printf("Could not find item2 to delete.\n");
    }

    if (find(head, "item2") == NULL) {
        printf("item2 is no longer in the list.\n");
    }
    
    delete_item(&head, "item1");
    delete_item(&head, "item3");

    return 0;
}