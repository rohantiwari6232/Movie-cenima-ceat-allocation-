#pragma once
#include <string>
#include <vector>
#include <cctype>

// Prefix tree for movie titles (case-insensitive, supports spaces/digits)
class Trie {
    struct Node {
        Node* next[128] = {nullptr};
        bool end = false;
        std::string title;           // original title stored at terminal node
    };
    Node* root = new Node();

    static void freeAll(Node* n) {
        if (!n) return;
        for (auto* c : n->next) freeAll(c);
        delete n;
    }
    static void collect(Node* n, std::vector<std::string>& out) {
        if (!n) return;
        if (n->end) out.push_back(n->title);
        for (auto* c : n->next) collect(c, out);
    }
public:
    ~Trie() { freeAll(root); }

    void insert(const std::string& title) {
        Node* cur = root;
        for (unsigned char ch : title) {
            ch = (unsigned char)std::tolower(ch);
            if (ch >= 128) continue;
            if (!cur->next[ch]) cur->next[ch] = new Node();
            cur = cur->next[ch];
        }
        cur->end = true;
        cur->title = title;
    }

    // O(L + k) autocomplete; empty prefix returns all titles
    std::vector<std::string> autocomplete(const std::string& prefix) const {
        Node* cur = root;
        for (unsigned char ch : prefix) {
            ch = (unsigned char)std::tolower(ch);
            if (ch >= 128) continue;
            if (!cur->next[ch]) return {};
            cur = cur->next[ch];
        }
        std::vector<std::string> out;
        collect(cur, out);
        return out;
    }
};
