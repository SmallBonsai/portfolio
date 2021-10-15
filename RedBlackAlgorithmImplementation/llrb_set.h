#ifndef LLRB_SET_H_
#define LLRB_SET_H_

#include <iostream>
#include <memory>
#include <string>
#include <utility>

template <typename K>
class LLRB_set {
public:
    // Return size of tree
    unsigned int Size();
    // Return whether @key is found in tree
    bool Contains(const K& key);
    // Return max key in tree
    const K& Max();
    // Return min key in tree
    const K& Min();
    // Insert @key in tree
    void Insert(const K &key);
    // Remove @key from tree
    void Remove(const K &key);
    // Print tree in-order
    void Print();

private:
    enum Color { RED, BLACK };
    struct Node{
        K key;
        bool color;
        std::unique_ptr<Node> left;
        std::unique_ptr<Node> right;
    };
    std::unique_ptr<Node> root;
    unsigned int cur_size = 0;

    // Iterative helper methods
    Node* Get(Node *n, const K &key);

    // Recursive helper methods
    Node* Min(Node *n);
    void Insert(std::unique_ptr<Node> &n, const K &key);
    void Remove(std::unique_ptr<Node> &n, const K &key);
    void Print(Node *n);

    // Helper methods for the self-balancing
    bool IsRed(Node *n);
    void FlipColors(Node *n);
    void RotateRight(std::unique_ptr<Node> &prt);
    void RotateLeft(std::unique_ptr<Node> &prt);
    void FixUp(std::unique_ptr<Node> &n);
    void MoveRedRight(std::unique_ptr<Node> &n);
    void MoveRedLeft(std::unique_ptr<Node> &n);
    void DeleteMin(std::unique_ptr<Node> &n);
};

template <typename K>
unsigned int LLRB_set<K>::Size() {
    return cur_size;
}

template <typename K>
typename LLRB_set<K>::Node* LLRB_set<K>::Get(LLRB_set<K>::Node* n,
                                             const K &key) {
    while (n) {
        if (key == n->key)
            return n;

        if (key < n->key)
            n = n->left.get();
        else
            n = n->right.get();
    }
    return nullptr;
}

template <typename K>
bool LLRB_set<K>::Contains(const K &key) {
    return Get(root.get(), key) != nullptr;
}

template <typename K>
const K& LLRB_set<K>::Max(void) {
    Node *n = root.get();
    while (n->right) n = n->right.get();
    return n->key;
}

template <typename K>
const K& LLRB_set<K>::Min(void) {
    return Min(root.get())->key;
}

template <typename K>
typename LLRB_set<K>::Node* LLRB_set<K>::Min(Node *n) {
    if (n->left)
        return Min(n->left.get());
    else
        return n;
}

template <typename K>
bool LLRB_set<K>::IsRed(Node *n) {
    if (!n) return false;
    return (n->color == RED);
}

template <typename K>
void LLRB_set<K>::FlipColors(Node *n) {
    n->color = !n->color;
    n->left->color = !n->left->color;
    n->right->color = !n->right->color;
}

template <typename K>
void LLRB_set<K>::RotateRight(std::unique_ptr<Node> &prt) {
    std::unique_ptr<Node> chd = std::move(prt->left);
    prt->left = std::move(chd->right);
    chd->color = prt->color;
    prt->color = RED;
    chd->right = std::move(prt);
    prt = std::move(chd);
}

template <typename K>
void LLRB_set<K>::RotateLeft(std::unique_ptr<Node> &prt) {
    std::unique_ptr<Node> chd = std::move(prt->right);
    prt->right = std::move(chd->left);
    chd->color = prt->color;
    prt->color = RED;
    chd->left = std::move(prt);
    prt = std::move(chd);
}

template <typename K>
void LLRB_set<K>::FixUp(std::unique_ptr<Node> &n) {
    // Rotate left if there is a right-leaning red node
    if (IsRed(n->right.get()) && !IsRed(n->left.get()))
        RotateLeft(n);
    // Rotate right if red-red pair of nodes on left
    if (IsRed(n->left.get()) && IsRed(n->left->left.get()))
        RotateRight(n);
    // Recoloring if both children are red
    if (IsRed(n->left.get()) && IsRed(n->right.get()))
        FlipColors(n.get());
}

template <typename K>
void LLRB_set<K>::MoveRedRight(std::unique_ptr<Node> &n) {
    FlipColors(n.get());
    if (IsRed(n->left->left.get())) {
        RotateRight(n);
        FlipColors(n.get());
    }
}

template <typename K>
void LLRB_set<K>::MoveRedLeft(std::unique_ptr<Node> &n) {
    FlipColors(n.get());
    if (IsRed(n->right->left.get())) {
        RotateRight(n->right);
        RotateLeft(n);
        FlipColors(n.get());
    }
}

template <typename K>
void LLRB_set<K>::DeleteMin(std::unique_ptr<Node> &n) {
    // No left child, min is 'n'
    if (!n->left) {
        // Remove n
        n = nullptr;
        return;
    }

    if (!IsRed(n->left.get()) && !IsRed(n->left->left.get()))
        MoveRedLeft(n);

    DeleteMin(n->left);

    FixUp(n);
}

template <typename K>
void LLRB_set<K>::Remove(const K &key) {
    if (!Contains(key))
        return;
    Remove(root, key);
    cur_size--;
    if (root)
        root->color = BLACK;
}

template <typename K>
void LLRB_set<K>::Remove(std::unique_ptr<Node> &n, const K &key) {
    // Key not found
    if (!n) return;

    if (key < n->key) {
        if (!IsRed(n->left.get()) && !IsRed(n->left->left.get()))
            MoveRedLeft(n);
        Remove(n->left, key);
    } else {
        if (IsRed(n->left.get()))
            RotateRight(n);

        if (key == n->key && !n->right) {
            // Remove n
            n = nullptr;
            return;
        }

        if (!IsRed(n->right.get()) && !IsRed(n->right->left.get()))
            MoveRedRight(n);

        if (key == n->key) {
            // Find min node in the right subtree
            Node *n_min = Min(n->right.get());
            // Copy content from min node
            n->key = n_min->key;
            // Delete min node recursively
            DeleteMin(n->right);
        } else {
            Remove(n->right, key);
        }
    }

    FixUp(n);
}

template <typename K>
void LLRB_set<K>::Insert(const K &key) {
    Insert(root, key);
    cur_size++;
    root->color = BLACK;
}

template <typename K>
void LLRB_set<K>::Insert(std::unique_ptr<Node> &n, const K &key) {
    if (!n)
        n = std::unique_ptr<Node>(new Node{key, RED});
    else if (key < n->key)
        Insert(n->left, key);
    else if (key > n->key)
        Insert(n->right, key);
    else
        throw std::runtime_error("Key already inserted");

    FixUp(n);
}

template <typename K>
void LLRB_set<K>::Print() {
    Print(root.get());
    std::cout << std::endl;
}

template <typename K>
void LLRB_set<K>::Print(Node *n) {
    if (!n) return;
    Print(n->left.get());
    std::cout << "<" << n->key << "> ";
    Print(n->right.get());
}

#endif  // LLRB_SET_H_
