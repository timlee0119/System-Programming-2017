#include <iostream>
#include <cstring>
#include <string>
#include <map>
#include <vector>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <errno.h>
#include "json.hpp"
#include "inf-bonbon-server.h"

using namespace std;
using json = nlohmann::json;
#define MAX_FD 1024
#define BUF_SIZE 4096

// store all clients' information
// 用法：fd_info_map.at(4) 代表fd為4的用戶的Client_info
vector<Client_info> fd_info_map;

// queue of clients' try_match
vector<int> match_queue;

bool parse_command(string command_string, int fd);
bool handle_command(json json_command, int fd);
int try_match(json json_command, int fd);

int main(int argc, char** argv)
{
	if (argc != 2) {
		cerr << "用法： ./inf-bonbon-server [port]\n";
		exit(0);
	}

	/* === set up server socket === */
	int port = atoi(argv[1]);
	int sockfd = socket(AF_INET , SOCK_STREAM , 0);
	assert(sockfd >= 0);
	// set up sockaddr_in
	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr)); // 清零初始化，不可省略
	server_addr.sin_family = PF_INET;              // 位置類型是網際網路位置
	server_addr.sin_addr.s_addr = INADDR_ANY;      // INADDR_ANY 是特殊的IP位置，表示接受所有外來的連線
	server_addr.sin_port = htons(port);           // 在 port 號 TCP 埠口監聽新連線
	// bind server socket to the IP address
	int bind_status = bind(sockfd, (struct sockaddr*) &server_addr, sizeof(server_addr)); // 綁定 sockfd 的位置
	if (bind_status == -1) {
		sleep(0.8);
		cout << "socket fail\n";
		exit(0);
	}

	// 呼叫 listen() 告知作業系統這是一個伺服socket
	int retval = listen(sockfd, 5);
	assert(!retval);
	/* ============================= */


	// 預留800個位置給match queue
	match_queue.resize(800);
	// 紀錄目前最大的client socket fd
	int cur_max_fd = 0;

	// 宣告 select() 使用的資料結構
	fd_set readset;
	fd_set working_readset;
	FD_ZERO(&readset);
	// 將 socket 檔案描述子放進 readset
	FD_SET(sockfd, &readset);
	// listen socket file descriptors
	while (1)
	{
	    memcpy(&working_readset, &readset, sizeof(fd_set));
	    int sel_num = select(MAX_FD, &working_readset, NULL, NULL, NULL);

	    if (sel_num < 0) // 發生錯誤
	    {
	        perror("select() went wrong");
	        exit(errno);
	    }

	    if (sel_num == 0) // 排除沒有事件的情形
	        continue;

	    for (int fd = 0; fd < MAX_FD; fd += 1) // 用迴圈列舉描述子
	    {
	        // 排除沒有事件的描述子
	        if (!FD_ISSET(fd, &working_readset))
	            continue;

	        // 分成兩個情形：接受新連線用的 socket 和資料傳輸用的 socket
	        if (fd == sockfd)
	        {
	            // sockfd 有事件，表示有新連線
	            struct sockaddr_in client_addr;
	            socklen_t addrlen = sizeof(client_addr);
	            int client_fd = accept(fd, (struct sockaddr*) &client_addr, &addrlen);
	            if (client_fd >= 0)
	                FD_SET(client_fd, &readset); // 加入新創的描述子，用於和客戶端連線

	            cout << "new client connected, fd: " << client_fd << endl;

	            // 紀錄目前最大client fd，以修改fd_info_map長度
	            if (client_fd > cur_max_fd) {
		            fd_info_map.resize(client_fd+1);
		            cur_max_fd = client_fd;
	            }

	            // 將client_fd加入fd_info_map，並將status設為閒置狀態、初始化結構
	            fd_info_map.at(client_fd).status = "idle";
	        }

	        /* TODO */
	        else
	        {
	        	cout << "recieve client command from fd:" << fd << endl;
	            ssize_t sz;
	            char buf[BUF_SIZE+1] = {0};
	            string command_string;

	            sz = recv(fd, buf, BUF_SIZE, 0);
	            if (sz == 0) // recv() 回傳值爲零表示客戶端已關閉連線
	            {
	                // 關閉描述子並從 readset 移除
	                close(fd);
	                FD_CLR(fd, &readset);
	            	cout << "close fd:" << fd << endl;
	            }
	            else if (sz < 0) // 發生錯誤
	            {
	                perror("recv from client error\n");
	                exit(0);
	            }
	            else // sz > 0，表示有新資料讀入
	            {
	            	buf[sz] = '\0';
	                command_string += buf;
	                while (sz == BUF_SIZE || buf[sz-2] != '}') //代表可能還有資料沒讀完
	                {
	                	if (buf[sz-2] != '}')
	                		command_string.pop_back();

	                	sz = recv(fd, buf, BUF_SIZE, 0);
	                	buf[sz] = '\0';
	                	command_string += buf;
	                }

	                cout << "recieve command string:" << command_string; //debug

	                // parse the json command and handle it
	                if (!parse_command(command_string, fd)) {
	                	perror("parse recieved command fault\n");
	                	exit(0);
	                }
	                // TODO...
	            }
	        }
	    }
	}





	// 結束程式前記得關閉 sockfd
	close(sockfd);

	return 0;
}

bool parse_command(string command_string, int fd)
{
	// find "\n" to see whether it contains more than one json commands
	size_t start = 0, found = -1;
	do {
		found = command_string.find("\n", start);
		if (found == string::npos) {
			cerr << "can't find endline in command string\n";
			return false;
		}

		// parse command to json object
		string command = command_string.substr(start, found-start);
		auto json_command = json::parse(command);
		// cout << json_command.dump(4) << endl; //debug

		// handle json command
		handle_command(json_command, fd);

		start = found+1;
	} while (found != command_string.length()-1);

	return true;
}

bool handle_command(json json_command, int fd)
{
	string cmd = json_command.at("cmd");
	
	if (cmd == "try_match")
	{
		cout << "handle try_match\n";

		// send confirm message
		const char* res_char = "{\"cmd\":\"try_match\"}\n";
		printf("send response message: %sto fd: %d\n", res_char, fd);
		send(fd, res_char, strlen(res_char), 0);

		// start matching
		int match_fd = try_match(json_command, fd);

		/* TODO */
		if (!match_fd)
		{
			// 配對不到人，把自己排進佇列
			// match_queue.push_back(fd)
		}
		else
		{
			// 配對到人，把配對到的fd移出佇列，並彼此記錄配對到的fd
		}
	}

	else if (cmd == "send_message")
	{
		/* TODO */

		cout << "handle send_message\n";
	}

	else if (cmd == "quit")
	{
		/* TODO */

		cout << "handle quit\n";
		// send confirm message
		const char* res_char = "{\"cmd\":\"quit\"}\n";
		printf("send response message to fd %d: %s", fd, res_char);
		send(fd, res_char, strlen(res_char), 0);
	}

	else {
		cerr << "incorrect cmd!\n";
		return false;
	}


	return true;
}

// return 配對到的fd，若沒配對到，回傳-1
int try_match(json json_command, int fd)
{
	int match_fd = -1;

	// 將該fd的status設為"matching"
	if (fd_info_map.at(fd).status == "idle") {
		fd_info_map.at(fd).status = "matching";
	}
	else {
		cerr << "try_match not from status \"idle\"\n";
	}

	// 設定該fd的user_info
	string name = json_command.at("name");
	strcpy(fd_info_map.at(fd).user_info.name, name.c_str());
	string gender = json_command.at("gender");
	strcpy(fd_info_map.at(fd).user_info.gender, gender.c_str());
	string introduction = json_command.at("introduction");
	strcpy(fd_info_map.at(fd).user_info.introduction, introduction.c_str());
	fd_info_map.at(fd).user_info.age = json_command.at("age");

	// 創建、編譯並load自己的share library
	char filter_func_cfile[16];
	sprintf(filter_func_cfile, "%d_filter.c", fd);

	int f = open(filter_func_cfile, O_CREAT | O_WRONLY | O_TRUNC | O_APPEND, S_IRUSR | S_IWUSR);
	if (f < 0) {
    	perror("open filter function c file fail\n");
    	exit(0);
	} 

	// 創造.c檔並編譯成.so
	// write struct User
	const char* struct_buf = "struct User {char name[33];unsigned int age;char gender[7];char introduction[1025];};\n";
	int b = write(f, struct_buf, strlen(struct_buf));
	if (b < 0) {
		perror("write filter_function.c file fail\n");
		exit(0);
	}

	// write filter function
	char func_buf[4097];
	string func_str = json_command.at("filter_function");
	strcpy(func_buf, func_str.c_str());
	int b2 = write(f, func_buf, strlen(func_buf));
	if (b2 < 0) {
		perror("write filter_function.c file fail\n");
		exit(0);
	}
	close(f);

	// compile .c file to .so file
	char cmd[128] = {0};
	char so_file[16] = {0};
	sprintf(so_file, "./%d_filter.so", fd);
	sprintf(cmd, "gcc -fPIC -O2 -std=c11 %s -shared -o %s"
		, filter_func_cfile, so_file);
	int sys_status = system(cmd);

	// handle error
	if (sys_status < 0 || sys_status == 127) {
		fprintf(stderr, "cmd: %s fail\n", cmd);
		exit(0);
	}

	/* TODO: 載入動態函式庫 */
    void* handle = dlopen(so_file, RTLD_LAZY);
    assert(NULL != handle);	
    // 載入 filter_function 函數
    dlerror();
    int (*my_filter)(struct User) = (int (*)(struct User)) dlsym(handle, "filter_function");
    const char *dlsym_error = dlerror();
    if (dlsym_error)
    {
        fprintf(stderr, "Cannot load symbol 'filter_function': %s\n", dlsym_error);
        dlclose(handle);
        return 1;
    }

	fd_set readset;
	fd_set working_readset;
	FD_ZERO(&readset);
	FD_SET(fd, &readset);
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
	for (int i = 0; i < match_queue.size(); i++)
	{
		int matching_fd = match_queue.at(i);
		memcpy(&working_readset, &readset, sizeof(fd_set));
		select(fd+1, &working_readset, NULL, NULL, )
		/* TODO */

	}

    // 最後記得關閉 handle
    dlclose(handle);

	return match_fd;
}


		// create response json message
		// json r;
		// r["cmd"] = "try_match";
		// string res_str = r.dump();
		// // don't forget to add "\n"
		// res_str += "\n";
		// const char* res_char = res_str.c_str();