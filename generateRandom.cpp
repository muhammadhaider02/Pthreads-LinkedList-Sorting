#include <iostream>
#include <fstream>
#include <cstdlib>
#include <ctime>

using namespace std;

int main() {
    ofstream outFile("inputFile.txt");

    if (!outFile) {
        cerr << "Error opening file!" << endl;
        return 1;
    }

    srand(time(0));

    int size = 1000000;
    
    for (int i = 0; i < size; ++i) {
        int randomNum = rand() % 9000 + 1000;
        outFile << randomNum << endl;
    }

    outFile.close();
    cout << "inputFile.txt now has " << size << " numbers!" << endl;

    return 0;
}
