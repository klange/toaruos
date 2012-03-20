#include <stdio.h>
#include <stdlib.h>
#include <queue>
#include <string>
#include <fstream>
#include <iostream>

class test {
	private:
		int i;
	public:
		test() {
			i = 0;
		}
		void inc() {
			i++;
		}
		int value() {
			return i;
		}
};

int main() {
	std::ios_base::Init();
	std::cout << "Hello world\n";

	int * x = (int *)malloc(sizeof(int));
	printf("%p\n", x);
	test * t = new test();
	printf("%d\n", t->value());
	t->inc();
	printf("%d\n", t->value());

	std::queue<int> q;
	q.push(4);
	q.push(20);
	q.push(5);
	q.push(19);

	while (!q.empty()) {
		printf("%d\n", q.front());
		q.pop();
	}

	std::string s;

	std::cin >> s;

	printf("%d: %s\n", s.length(), s.c_str());


	return 0;
}
