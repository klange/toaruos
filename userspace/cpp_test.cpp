/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * C++ Test Program
 */
#include <stdio.h>
#include <stdlib.h>
#include <queue>
#include <string>
#include <fstream>
#include <iostream>

/* A class */
class test {
	private:
		/* With a little private integer */
		int i;
	public:
		test() {
			/* That starts at zero */
			i = 0;
		}
		void inc() {
			/* And can be incremented */
			i++;
		}
		int value() {
			/* And can have its value retreived */
			return i;
		}
};

int main() {
	/*
	 * HACK: We need to execute ios_base::Init() to ensure the base
	 * IO objects are accessible (this would normally be called by
	 * the static constructor for std::__ioinit, but we don't currently
	 * execute static constructurs as we never execute the .init section)
	 */
	std::ios_base::Init();

	/*
	 * First test: Print "Hello world" and a new line.
	 */
	std::cout << "Hello world\n";

	/* Create a test object */
	test * t = new test();
	/* Print its value = 0 */
	printf("%d\n", t->value());
	/* Increment it */
	t->inc();
	/* And print again = 1 */
	printf("%d\n", t->value());

	/* Create a queue */
	std::queue<int> q;
	q.push(4);
	q.push(20);
	q.push(5);
	q.push(19);

	/* Print out the values in order */
	while (!q.empty()) {
		printf("%d\n", q.front());
		q.pop();
	}

	/* Read in a string */
	std::string s;
	std::cin >> s;

	/* Print it and its length */
	printf("%d: %s\n", s.length(), s.c_str());

	return 0;
}
