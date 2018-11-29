#include <iostream>
#include <string>
#include <fstream>
using namespace std;

int main(int argc, char* argv[])
{
	// catch the arguments
	string input_str = argv[1];
	string file_name = "";
	if (argc == 3)
		file_name = argv[2];

	// read file
	if (file_name != "")
	{
		ifstream input_file;
		input_file.open(file_name.c_str());

		char c;
		char ischarset[256] = {0};
		for (int i = 0; i < input_str.size(); i++)
		{
			ischarset[input_str[i]] = 1;
		}

		if (input_file)
		{
			int times = 0;
			while (input_file.get(c))
			{
				if (c == '\n')
				{
					cout << times << endl;
					times = 0;
					continue;
				}
				if (ischarset[c] == 1)
					times++;
			}

			input_file.close();
		}

		// file name error
		else
			cerr << "error\n";
	}

	// standard input
	else 
	{
		char c;
		char ischarset[256] = {0};
		for (int i = 0; i < input_str.size(); i++)
		{
			ischarset[input_str[i]] = 1;
		}
		int times = 0;
		while (cin.get(c))
		{
			if (c == '\n')
			{
				cout << times << endl;
				times = 0;
				continue;
			}
			if (ischarset[c] == 1)
				times++;
		}
	}

	return 0;
}
