#include <iostream>
#include <cstring>
#include <string>
#include <fstream>
#include <vector>
#include <map>
#include <algorithm>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include "md5.c"
#include "list_file.c"
using namespace std;

#define READ_DATA_SIZE  1024
#define MD5_SIZE        16
#define MD5_STR_LEN     (MD5_SIZE * 2)
const string new_file_tag = "[new_file]\n", modified_tag = "[modified]\n",
	  copied_tag = "[copied]\n", MD5_tag = "(MD5)\n";

int Compute_file_md5(const char *file_path, char *value);

int main(int argc, char* argv[])
{
	// change working directory
	if (chdir(argv[argc-1]) == -1)
		return -1;

	// check .loser_config
	map<string, string> command_map;
	command_map["status"] = "status";
	command_map["commit"] = "commit";
	command_map["log"] = "log";

	ifstream config;
	config.open(".loser_config");
	if (config)
	{
		string buffer, alter, type;
		while(getline(config, buffer))
		{
			int index = 0;
			for (int i = 0; i < buffer.length(); i++)
			{
				if (buffer[i] == '=')
				{
					alter = buffer.substr(0,i-1);
					index = i;
					break;
				}
			}
			type = buffer.substr(index+2, buffer.length()-index-2);
			command_map[alter] = type;
		}

		config.close();
	}
	
	// execute status or commit
	string command = argv[1];
	//cout << command << command_map[command] << endl;
	if (argc == 3)
	{
		// argv[1] is command, argc[2] is directory name
		if (command_map[command] == "status")
		{
			ifstream record_file;
			record_file.open(".loser_record", ios::ate);

			// the outputs
			vector<string> new_file_v, modified_v, copied_v;

			// if .loser_record doesn't exist
			if (record_file == 0)
			{
				struct FileNames file_names = list_file(".");
				//cout << new_file_tag;
				for (int i = file_names.length-1; i >= 0; i--)
				{
					if (strcmp(file_names.names[i], ".") == 0 || 
						strcmp(file_names.names[i], "..") == 0 ||
						strcmp(file_names.names[i], ".loser_record") == 0)
						continue;
					//cout << file_names.names[i] << endl;
					new_file_v.push_back(file_names.names[i]);
				}
				free_file_names(file_names);
				//cout << modified_tag << copied_tag;
				cout << new_file_tag;
				sort(new_file_v.begin(), new_file_v.end());
				for (int i = 0; i < new_file_v.size(); i++)
					cout << new_file_v[i] << endl;
				cout << modified_tag << copied_tag;
			}

			// if .loser_record exists
			else
			{
				// set the file pointer to the next line of (MD5)
				streampos record_file_size = record_file.tellg();
				for (int i = 1; i < record_file_size; i++)
				{
					record_file.seekg(-i, ios::end);
					if (record_file.get() == ')')
					{
						record_file.seekg(1, ios::cur);
						break;
					}
				}
				// all files in directory
				struct FileNames file_names = list_file(".");

				// old name or md5 value=0, omitted file value=-1
				vector<int> is_new_name(file_names.length, 1);
				vector<int> is_new_md5(file_names.length, 1);
				map<int, string> copied_map;

				char record_name[256];
				char record_md5[33];
				char file_md5[MD5_STR_LEN+1];
				while (record_file.getline(record_name, 256, ' '))
				{
					record_file.getline(record_md5, 33);
					for (int i = file_names.length-1; i >= 0; i--)
					{
						// ignore
						if (strcmp(file_names.names[i], ".") == 0 || 
							strcmp(file_names.names[i], "..") == 0 ||
							strcmp(file_names.names[i], ".loser_record") == 0)
						{
							is_new_name[i] = -1;
							is_new_md5[i] = -1;
							continue;
						}

						// prepare file md5
						Compute_file_md5(file_names.names[i] ,file_md5);

						if (strcmp(record_name, file_names.names[i]) == 0)
						{
							is_new_name[i] = 0;
							// determine modified
							if (strcmp(record_md5, file_md5) != 0)
							{
								modified_v.push_back(file_names.names[i]);
							}
						}
						else if (strcmp(record_md5, file_md5) == 0)
						{
							is_new_md5[i] = 0;
							if (copied_map.count(i) == 0)
								copied_map[i] = record_name;
						}
					}
				}

				// determine new_file and copied
				// if it's a new name and new md5, new_file
				// if it's a new name but old md5, copied 
				for (int i = file_names.length-1; i >= 0; i--)
				{
					// ignore
					if (is_new_name[i] == -1)
						continue;

					if (is_new_name[i] == 1)
					{
						if (is_new_md5[i] == 1)
						{
							new_file_v.push_back(file_names.names[i]);
						}
						else
						{
							string str = copied_map[i];
							str += " => ";
							str += file_names.names[i];
							copied_v.push_back(str);
						}
					}
				}

				free_file_names(file_names);
				// status output!!!
				sort(new_file_v.begin(), new_file_v.end());
				sort(modified_v.begin(), modified_v.end());
				sort(copied_v.begin(), copied_v.end());
				cout << new_file_tag;
				for (int i = 0; i < new_file_v.size(); i++)
					cout << new_file_v[i] << endl;
				cout << modified_tag;
				for (int i = 0; i < modified_v.size(); i++)
					cout << modified_v[i] << endl;
				cout << copied_tag;
				for (int i = 0; i < copied_v.size(); i++)
					cout << copied_v[i] << endl;
				// cout << new_file << modified << copied;
			}

			record_file.close();
		}

		else if (command_map[command] == "commit")
		{
			struct FileNames file_names = list_file(".");
			// no file in directory
			if (file_names.length == 2)
				return 0;

			// create .loser_record if not exist
			bool no_commit = 0;
			fstream record_file;
			record_file.open(".loser_record", ios::in | ios::ate);
			if (record_file == 0)
			{
				no_commit = 1;
				record_file.open(".loser_record", ios::out);
				record_file.open(".loser_record", ios::in | ios::ate);
			}

			// the outputs
			vector<string> new_file_v, modified_v, copied_v, MD5_v;	
			int commit_num = 0;		

			// get last commit number
			char commit_num_str[256];
			streampos record_file_size = record_file.tellg();
			for (int i = 1; i <= record_file_size; i++)
			{
				record_file.seekg(-i, ios::end);
				if (record_file.get() == '#')
				{
					record_file.seekg(8, ios::cur);
					record_file.getline(commit_num_str, 256);
					commit_num = atoi(commit_num_str);
					break;
				}
			}

			// move to the next line of (MD5)
			char c;
			while (record_file.get(c))
			{
				if (c == ')')
				{
					record_file.seekg(1, ios::cur);
					break;
				}
			}

			// start checking status, including all files' MD5
			vector<int> is_new_name(file_names.length, 1);
			vector<int> is_new_md5(file_names.length, 1);
			map<int, string> copied_map;
			char file_md5[MD5_STR_LEN+1];
			for (int i = 0; i < file_names.length; i++)
			{
				if (strcmp(file_names.names[i], ".") == 0 || 
					strcmp(file_names.names[i], "..") == 0 ||
					strcmp(file_names.names[i], ".loser_record") == 0)
					continue;

				Compute_file_md5(file_names.names[i] ,file_md5);
				string md5_out = file_names.names[i];
				md5_out += " ";
				md5_out += file_md5;
				MD5_v.push_back(md5_out);							
			}

			char record_name[256];
			char record_md5[33];
			while (record_file.getline(record_name, 256, ' '))
			{
				record_file.getline(record_md5, 33);
				for (int i = 0; i < file_names.length; i++)
				{
					// ignore
					if (strcmp(file_names.names[i], ".") == 0 || 
						strcmp(file_names.names[i], "..") == 0 ||
						strcmp(file_names.names[i], ".loser_record") == 0)
					{
						is_new_name[i] = -1;
						is_new_md5[i] = -1;
						continue;
					}

					// prepare file md5 
					char file_md5[MD5_STR_LEN+1];
					Compute_file_md5(file_names.names[i] ,file_md5);

					if (strcmp(record_name, file_names.names[i]) == 0)
					{
						is_new_name[i] = 0;
						// determine modified
						if (strcmp(record_md5, file_md5) != 0)
						{
							modified_v.push_back(file_names.names[i]);
						}
					}
					else if (strcmp(record_md5, file_md5) == 0)
					{
						is_new_md5[i] = 0;
						if (copied_map.count(i) == 0)
							copied_map[i] = record_name;
					}
				}
			}

			// determine new_file and copied
			// if it's a new name and new md5, new_file
			// if it's a new name but old md5, copied 
			for (int i = file_names.length-1; i >= 0; i--)
			{
				// ignore
				if (is_new_name[i] == -1)
					continue;

				if (is_new_name[i] == 1)
				{
					if (is_new_md5[i] == 1)
					{
						new_file_v.push_back(file_names.names[i]);
					}
					else
					{
						string str = copied_map[i];
						str += " => ";
						str += file_names.names[i];
						copied_v.push_back(str);
					}
				}
			}

			free_file_names(file_names);
			record_file.close();

			// output to .loser_record
			if (new_file_v.size() == 0 && modified_v.size() == 0 && copied_v.size() == 0)
				return 0;

			sort(new_file_v.begin(), new_file_v.end());
			sort(modified_v.begin(), modified_v.end());
			sort(copied_v.begin(), copied_v.end());
			sort(MD5_v.begin(), MD5_v.end());

			ofstream record_commit;
			record_commit.open(".loser_record", ios::app);

			if (no_commit)
			{
				record_commit << "# commit 1\n";
				record_commit << new_file_tag;
				// omit "." and ".."
				for (int i = 2; i < new_file_v.size(); i++)
					record_commit << new_file_v[i] << endl;
			}
			else
			{
				record_commit << "\n# commit " << commit_num+1 << "\n";
				record_commit << new_file_tag;
				for (int i = 0; i < new_file_v.size(); i++)
					record_commit << new_file_v[i] << endl;
			}
			record_commit << modified_tag;
			for (int i = 0; i < modified_v.size(); i++)
				record_commit << modified_v[i] << endl;
			record_commit << copied_tag;
			for (int i = 0; i < copied_v.size(); i++)
				record_commit << copied_v[i] << endl;
			record_commit << MD5_tag;
			for (int i = 0; i < MD5_v.size(); i++)
				record_commit << MD5_v[i] << endl;
			record_commit.close();
		}		
	}

	// execute log
	else if (argc == 4)
	{
		if (command_map[command] == "log")
		{
			vector<char> output;
			int log_num = atoi(argv[2]);
			int log_times = log_num;
			bool first = true;

			ifstream record_file;
			record_file.open(".loser_record", ios::ate);
			if (record_file == 0)
				return 0;

			// get '#'
			streampos record_file_size = record_file.tellg();
			for (int i = 1; i <= record_file_size; i++)
			{
				if (log_times == 0)
					break;

				record_file.seekg(-i, ios::end);
				char c;
				record_file.get(c);
				output.push_back(c);
				if (c == '#')
				{
					if (first)
					{
						for (int i = output.size()-1; i >= 0; i--)
							cout << output[i];
						first = false;			
					}
					else
					{
						cout << "\n";
						// omit the last "\n"
						for (int i = output.size()-1; i > 0; i--)
							cout << output[i];
					}

					output.clear();
					log_times--;
				}
			}

			record_file.close();
		}
	}


	return 0;
}

int Compute_file_md5(const char *file_path, char *md5_str)
{
    int i;
    int fd;
    int ret;
    unsigned char data[READ_DATA_SIZE];
    unsigned char md5_value[MD5_SIZE];
    MD5_CTX md5;

    fd = open(file_path, O_RDONLY);
    if (-1 == fd)
    {
        perror("open");
        return -1;
    }

    // init md5
    MD5Init(&md5);

    while (1)
    {
        ret = read(fd, data, READ_DATA_SIZE);
        if (-1 == ret)
        {
            perror("read");
            return -1;
        }

        MD5Update(&md5, data, ret);

        if (0 == ret || ret < READ_DATA_SIZE)
        {
            break;
        }
    }

    close(fd);

    MD5Final(&md5, md5_value);

    for(i = 0; i < MD5_SIZE; i++)
    {
        snprintf(md5_str + i*2, 2+1, "%02x", md5_value[i]);
    }
    md5_str[MD5_STR_LEN] = '\0'; // add end

    return 0;
}