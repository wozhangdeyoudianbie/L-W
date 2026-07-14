#include <cassert>
#include <iostream>
#include <string>
#include "buffer.h"

using namespace std;

void test_initial_state()
{
    Buffer buf;
    assert(buf.empty());
    assert(buf.readable_bytes() == 0);
    assert(buf.writeable_bytes() == 4096);
    assert(buf.prependable_bytes() == 0);
}

void test_append_and_retrieve()
{
    Buffer buf(16);
    string data = "abcdef";
    buf.append(data);
    assert(!buf.empty());
    assert(buf.readable_bytes() == 6);
    assert(buf.writeable_bytes() == 10);
    assert(string(buf.peek(), buf.readable_bytes()) == "abcdef");

    buf.retrieve(2);
    assert(buf.readable_bytes() == 4);
    assert(buf.prependable_bytes() == 2);
    assert(string(buf.peek(), buf.readable_bytes()) == "cdef");

    string result = buf.retrieve_all_as_string();
    assert(result == "cdef");
    assert(buf.empty());
    assert(buf.readable_bytes() == 0);
    assert(buf.writeable_bytes() == 16);
    assert(buf.prependable_bytes() == 0);
}

void test_move_space()
{
    Buffer buf(10);
    buf.append("abcdef", 6);
    buf.retrieve(4);

    assert(buf.readable_bytes() == 2);
    assert(buf.writeable_bytes() == 4);
    assert(buf.prependable_bytes() == 4);

    buf.append("ghijkl", 6);

    assert(buf.readable_bytes() == 8);
    assert(buf.writeable_bytes() == 2);
    assert(buf.prependable_bytes() == 0);
    assert(string(buf.peek(), buf.readable_bytes()) == "efghijkl");
}

void test_resize_space()
{
    Buffer buf(8);
    buf.append("abcdef", 6);
    buf.retrieve(1);

    assert(buf.readable_bytes() == 5);
    assert(buf.writeable_bytes() == 2);
    assert(buf.prependable_bytes() == 1);

    buf.append("ghijkl", 6);

    assert(buf.readable_bytes() == 11);
    assert(buf.writeable_bytes() == 0);
    assert(buf.prependable_bytes() == 1);
    assert(string(buf.peek(), buf.readable_bytes()) == "bcdefghijkl");
}

void test_binary_data()
{
    Buffer buf;
    const char data[] = {'A', '\0', 'B'};
    buf.append(data, 3);

    assert(buf.readable_bytes() == 3);

    string result = buf.retrieve_all_as_string();
    assert(result.size() == 3);
    assert(result[0] == 'A');
    assert(result[1] == '\0');
    assert(result[2] == 'B');
    assert(buf.empty());
}

int main()
{
    test_initial_state();
    test_append_and_retrieve();
    test_move_space();
    test_resize_space();
    test_binary_data();
    cout << "Buffer 所有测试通过" << endl;
    return 0;
}
