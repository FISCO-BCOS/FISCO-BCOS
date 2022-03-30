#include "../../src/dag/TrieSet.h"
#include <vector>
#include <iostream>
#include <cstdlib>

using namespace std;
using namespace bcos::executor;

int main() {
    TrieSet<string, int> trie;

    vector<string> root = {"1", "2"};
    //vector<string> root = {"1", "2", "8"};

    vector<string> path1 = {"1", "2","4"};
    trie.set(path1, 4);

    vector<string> path2 = {"1", "2", "5"};
    trie.set(path2, 5);

    vector<string> path3 = {"1", "3", "6"};
    trie.set(path3, 6);

    vector<string> path4 = {"1", "2", "5", "7"};
    trie.set(path4, 7);

    for(auto v : trie.get(root)) {
        cout << v << " ";
    }
    return 0;
}

int main1() {
    TrieSet<string, int> trie;

    vector<string> root = {"1", "2"};

    vector<string> path1 = {"1", "2","4"};
    trie.set(path1, 4);

    vector<string> path2 = {"1", "2", "5"};
    trie.set(path2, 5);

    vector<string> path3 = {"1", "3", "6"};
    trie.set(path3, 6);

    vector<string> path4 = {"1", "2", "5", "7"};
    trie.set(path4, 7);

    for(auto v : trie.get(root)) {
        cout << v << " ";
    }
    return 0;
}

int main2() {
    TrieSet<int, int> trie;

    vector<int> root = {};

    for (int i = 0; i < 100; i++) {
        vector<int> path = {i / 10, i % 10};
        trie.set(path, i);
    }

    for(auto v : trie.get(root)) {
        cout << v << " ";
    }
    cout << endl;

    vector<int> path = {2};
    trie.set(path, 666);

    for(auto v : trie.get(root)) {
        cout << v << " ";
    }
    cout << endl;

    return 0;
}