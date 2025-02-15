#include <iostream>
#include <fstream>
#include <cmath>
#include <pthread.h>
#include <cstdlib>
#include <algorithm>
#include <sched.h>
#include <unistd.h>
#include <chrono>
#include <cmath>
#include <ctime>

using namespace std;
using namespace chrono;

static const int NUM_THREADS = sysconf(_SC_NPROCESSORS_ONLN);
static const int DEFAULT_NUM  = 1000000;

struct Node {
    int data;
    Node* next;
};

Node* globalHead = nullptr;
pthread_mutex_t listMutex;

// 1) Serial Version
void readRollNumbers(const char* filename, int* Numbers, int num) {
    ifstream inputFile(filename);
    if (!inputFile) {
        cerr << "Error opening file " << filename << endl;
        exit(1);
    }
    for (int i = 0; i < num; i++) {
        inputFile >> Numbers[i];
    }
    inputFile.close();
}

// Insert all numbers into a singly-linked list
void addRollNumbersToList(Node** head, int* Numbers, int num) {
    for (int i = 0; i < num; i++) {
        Node* newNode = new Node();
        newNode->data = Numbers[i];
        newNode->next = (*head);
        (*head) = newNode;
    }
}

// Find the tail of the linked list
Node* getTail(Node* cur) {
    while (cur && cur->next) {
        cur = cur->next;
    }
    return cur;
}

// Partition for serial QuickSort
Node* partitionSerial(Node* head, Node* end, Node** newHead, Node** newEnd) {
    Node* pivot = end;
    Node* prev = nullptr;
    Node* cur  = head;
    Node* tail = pivot;

    while (cur != pivot) {
        if (cur->data < pivot->data) {
            if (*newHead == nullptr) *newHead = cur;
            prev = cur;
            cur = cur->next;
        } else {
            if (prev) prev->next = cur->next;
            Node* tmp = cur->next;
            cur->next = nullptr;
            tail->next = cur;
            tail = cur;
            cur = tmp;
        }
    }
    if (*newHead == nullptr) *newHead = pivot;
    *newEnd = tail;
    return pivot;
}

// Recursive serial QuickSort
Node* quickSortRecSerial(Node* head, Node* end) {
    if (!head || head == end) {
        return head;
    }
    Node* newHead = nullptr;
    Node* newEnd  = nullptr;

    Node* pivot = partitionSerial(head, end, &newHead, &newEnd);

    // If pivot is not the smallest element
    if (newHead != pivot) {
        // break before pivot
        Node* tmp = newHead;
        while (tmp->next != pivot) tmp = tmp->next;
        tmp->next = nullptr;

        newHead = quickSortRecSerial(newHead, tmp);

        // reattach pivot
        tmp = getTail(newHead);
        tmp->next = pivot;
    }
    pivot->next = quickSortRecSerial(pivot->next, newEnd);
    return newHead;
}

Node* quickSortSerial(Node* head) {
    Node* end = getTail(head);
    return quickSortRecSerial(head, end);
}

// 2) Parallel Version

// CPU Affinity Helper
void setAffinity(pthread_t thread, int coreId) {
    int numCores = sysconf(_SC_NPROCESSORS_ONLN);
    if (coreId >= numCores) {
        coreId %= numCores;
    }
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(coreId, &cpuset);

    int result = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (result != 0) {
        cerr << "[Warning] Failed to set affinity for thread on core "
             << coreId << " (Check permissions or available CPUs)" << endl;
    }
}

// Parallel Insert Structures
struct InsertThreadData {
    int threadId;
    int startIndex;
    int endIndex;
    int* numbers;
};

// Each thread builds a local list, then attaches once
void* addRollNumbersToListParallel(void* arg) {
    InsertThreadData* data = (InsertThreadData*)arg;
    setAffinity(pthread_self(), data->threadId);

    Node* localHead = nullptr;
    Node* localTail = nullptr;

    for (int i = data->startIndex; i < data->endIndex; i++) {
        Node* newNode = new Node();
        newNode->data = data->numbers[i];
        newNode->next = nullptr;

        if (!localHead) {
            localHead = localTail = newNode;
        } else {
            localTail->next = newNode;
            localTail = newNode;
        }
    }
    // Attach local list -> globalHead
    if (localHead) {
        pthread_mutex_lock(&listMutex);
        localTail->next = globalHead;
        globalHead = localHead;
        pthread_mutex_unlock(&listMutex);
    }
    pthread_exit(nullptr);
}

// Partition for parallel QuickSort
Node* partitionParallel(Node* head, Node* end, Node** newHead, Node** newEnd) {
    Node* pivot = end;
    Node* prev  = nullptr;
    Node* cur   = head;
    Node* tail  = pivot;

    while (cur != pivot) {
        if (cur->data < pivot->data) {
            if (*newHead == nullptr) {
                *newHead = cur;
            }
            prev = cur;
            cur = cur->next;
        } else {
            if (prev) prev->next = cur->next;
            Node* tmp = cur->next;
            cur->next = nullptr;
            tail->next = cur;
            tail = cur;
            cur = tmp;
        }
    }
    if (*newHead == nullptr) {
        *newHead = pivot;
    }
    *newEnd = tail;
    return pivot;
}

// QSArgs for parallel recursion
struct QSArgs {
    Node* head;
    Node* end;
    int   depth;
    int   maxDepth;
};

Node* quickSortParallelRec(Node* head, Node* end, int depth, int maxDepth);

void* quickSortParallelThread(void* arg) {
    QSArgs* qarg = (QSArgs*)arg;
    setAffinity(pthread_self(), qarg->depth % NUM_THREADS);

    Node* sortedHead = quickSortParallelRec(qarg->head, qarg->end, qarg->depth, qarg->maxDepth);
    pthread_exit((void*)sortedHead);
}

// Parallel recursion
Node* quickSortParallelRec(Node* head, Node* end, int depth, int maxDepth) {
    if (!head || head == end) {
        return head;
    }

    Node* newHead = nullptr;
    Node* newEnd  = nullptr;

    Node* pivot = partitionParallel(head, end, &newHead, &newEnd);

    // sort left sublist
    if (newHead != pivot) {
        Node* tmp = newHead;
        while (tmp->next != pivot) {
            tmp = tmp->next;
        }
        tmp->next = nullptr;

        if (depth < maxDepth) {
            pthread_t leftThread;
            QSArgs leftArg {newHead, tmp, depth + 1, maxDepth};

            if (pthread_create(&leftThread, nullptr, quickSortParallelThread, &leftArg) == 0) {
                void* leftResult = nullptr;
                pthread_join(leftThread, &leftResult);
                newHead = (Node*)leftResult;
            } else {
                newHead = quickSortParallelRec(newHead, tmp, depth + 1, maxDepth);
            }
        } else {
            newHead = quickSortParallelRec(newHead, tmp, depth + 1, maxDepth);
        }

        // reattach pivot
        tmp = newHead;
        while (tmp && tmp->next) {
            tmp = tmp->next;
        }
        tmp->next = pivot;
    }

    // sort right sublist
    if (depth < maxDepth) {
        pthread_t rightThread;
        QSArgs rightArg {pivot->next, newEnd, depth + 1, maxDepth};

        if (pthread_create(&rightThread, nullptr, quickSortParallelThread, &rightArg) == 0) {
            void* rightResult = nullptr;
            pthread_join(rightThread, &rightResult);
            pivot->next = (Node*)rightResult;
        } else {
            pivot->next = quickSortParallelRec(pivot->next, newEnd, depth + 1, maxDepth);
        }
    } else {
        pivot->next = quickSortParallelRec(pivot->next, newEnd, depth + 1, maxDepth);
    }

    return newHead;
}

Node* quickSortParallel(Node* head) {
    if (!head) return head;
    Node* end = head;
    while (end->next) end = end->next;

    int maxDepth = (int)std::log2(NUM_THREADS) + 2;

    return quickSortParallelRec(head, end, 0, maxDepth);
}

void printList(Node* head) {
    while (head) {
        cout << head->data << " ";
        head = head->next;
    }
    cout << endl;
}

int main() {
    pthread_mutex_init(&listMutex, nullptr);

    int* Numbers = new int[DEFAULT_NUM];
    readRollNumbers("inputFile.txt", Numbers, DEFAULT_NUM);

    // Shuffle to ensure random pivot splits
    srand(time(nullptr));
    for (int i = DEFAULT_NUM - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        std::swap(Numbers[i], Numbers[j]);
    }

    cout << "[INPUT]    Size taken: " <<  DEFAULT_NUM << endl; 

    // A) SERIAL QS
    Node* serialHead = nullptr;
    addRollNumbersToList(&serialHead, Numbers, DEFAULT_NUM);
    
    auto startSerial = high_resolution_clock::now();
    serialHead = quickSortSerial(serialHead);
    auto endSerial = high_resolution_clock::now();
    
    duration<double, milli> serialTime = endSerial - startSerial;
    cout << "[SERIAL]   Time taken: " << serialTime.count() << " ms\n";

    // B) PARALLEL QS
    globalHead = nullptr;

    pthread_t threads[NUM_THREADS];
    InsertThreadData threadData[NUM_THREADS];

    int chunk = DEFAULT_NUM / NUM_THREADS;
    int leftover = DEFAULT_NUM % NUM_THREADS;
    int startIndex = 0;

    for (int i = 0; i < NUM_THREADS; i++) {
        threadData[i].threadId = i;
        threadData[i].numbers  = Numbers;
        int count = chunk + ((i == NUM_THREADS - 1) ? leftover : 0);
        threadData[i].startIndex = startIndex;
        threadData[i].endIndex   = startIndex + count;
        startIndex += count;

        pthread_create(&threads[i], nullptr, addRollNumbersToListParallel, &threadData[i]);
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], nullptr);
    }

    auto startParallel = high_resolution_clock::now();
    globalHead = quickSortParallel(globalHead);
    auto endParallel = high_resolution_clock::now();

    duration<double, milli> parallelTime = endParallel - startParallel;
    cout << "[PARALLEL] Time taken: " << parallelTime.count() << " ms\n";

    double speedup = serialTime.count() / parallelTime.count();
    cout << "Speedup Factor (Serial / Parallel): " << speedup << "x\n";

    delete[] Numbers;
    pthread_mutex_destroy(&listMutex);
    return 0;
}
