#include <string>

struct User {
	char name[33];
	unsigned int age;
	char gender[7];
	char introduction[1025];
};

// struct contains a client's User struct, fd, and .so file path
// status意義:
// "": 未連線, "idle": 閒置狀態, "matching":配對中, "chatting": 聊天中
struct Client_info {
	User user_info;
	std::string share_lib_path;
	std::string status;
};