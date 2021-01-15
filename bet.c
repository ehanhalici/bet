#include <stdio.h>
#include <stdlib.h>
#include <ncurses.h>
#include <locale.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#define ALT_BACKSPACE 127
#define ALT_ENTER 10
#define ALT_DELETE 330
#define ALT_TAB 9
#define ALT_CTRL_Q 17
#define ALT_CTRL_S 19
//not used
#define ALT_CTRL_C 3
#define ALT_CTRL_V 22
#define ALT_CTRL_A 1
//not used end
#define wchar unsigned int
#define input_device_stdin 0
#define input_device_file 1

typedef struct Row Row;
typedef struct Cursor Cursor;
typedef struct Page Page;
typedef struct File File;

//Row* türünde active row ekle adesini tut
//BUG overflowları page den row a taşı ve her ilerlemede kontrol et
struct Page
{
	Cursor *cursor;
	Row *row;
	int row_count;
	int max_y;
	int max_x;
	int screen_x;
	int screen_y;
};
Page page;

struct Cursor
{
	int y;
	int x;
	int lastX;
};

struct Row
{
	wchar *line;
	int length;
	Row *next;
};

struct File
{
	FILE* file_ptr;
	char *file_name;
};
File file;

void add_row()
{
	Row *ptr = page.row;
	for (int i = 0; i < page.cursor->y; i++)
		ptr = ptr->next;
	//en sona ekle
	if (ptr->next == NULL)
	{
		ptr->next = (Row *)malloc(sizeof(Row));
		ptr = ptr->next;

		ptr->line = NULL;
		ptr->length = 0;
		ptr->next = NULL;
	} //en başa ekle
	else if (ptr == page.row && page.cursor->x == 0)
	{
		Row *temp = (Row *)malloc(sizeof(Row));
		temp->next = ptr;
		ptr = temp;
		ptr->line = NULL;
		ptr->length = 0;
	} //araya ekle
	else
	{
		Row *temp = (Row *)malloc(sizeof(Row));
		temp->next = ptr->next;
		ptr->next = temp;
		ptr = ptr->next;
		ptr->line = NULL;
		ptr->length = 0;
	}
	page.row_count += 1;
}

void add_ch(int ch)
{
	Row *ptr = page.row;
	for (int i = 0; i < page.cursor->y; i++)
		ptr = ptr->next;

	if (ptr->line == NULL)
	{
		ptr->line = (wchar *)malloc(sizeof(wchar) * 2);
		ptr->length = 1;
	}
	else
	{
		ptr->line = (wchar *)realloc(ptr->line, sizeof(wchar) * (ptr->length + 2));
		ptr->length += 1;
	}
	ptr->line[ptr->length] = '\0';
	if (page.cursor->x == ptr->length - 1)
		ptr->line[ptr->length - 1] = ch;

	else
	{
		for (int i = ptr->length - 1; i > page.cursor->x; i--)
			ptr->line[i] = ptr->line[i - 1];

		ptr->line[page.cursor->x] = ch;
	}
}

void new_line(char ch)
{
	Row *ptr = page.row;
	int i = 0;
	for (; i < page.cursor->y; i++)
		ptr = ptr->next;

	//satırın ortasında bi yerde enter a basıldıysa
	if (ptr->length > page.cursor->x)
	{
		add_row();
		int x = page.cursor->x;
		page.cursor->y += 1;
		page.cursor->x = 0;
		for (int i = x; i < ptr->length; i++)
		{
			move(page.cursor->y, page.cursor->x);
			add_ch(ptr->line[i]);
			page.cursor->x += 1;
		}
		ptr->line[x] = '\0';
		ptr->line = (wchar *)realloc(ptr->line, sizeof(wchar) * (x + 1));
		ptr->length = x;
		page.cursor->x = 0;
	}
	else
	{
		add_row();
		page.cursor->y += 1;
		page.cursor->x = 0;
	}
}

void backspace()
{
	if (page.cursor->x > 0)
	{
		Row *ptr = page.row;
		for (int i = 0; i < page.cursor->y; i++)
			ptr = ptr->next;

		for (int i = page.cursor->x; i <= ptr->length - 1; i++)
			ptr->line[i - 1] = ptr->line[i];

		ptr->line = (wchar *)realloc(ptr->line, sizeof(wchar) * (ptr->length));
		ptr->length -= 1;
		page.cursor->x -= 1;
	}
	// bu satırı yok et ve bu satırın tüm elemanlarını bir üst satıra ekle
	else if (page.cursor->y != 0)
	{
		Row *ptr = page.row;
		for (int i = 0; i < page.cursor->y - 1; i++)
			ptr = ptr->next;

		Row *temp = ptr->next;
		ptr->next = temp->next;
		page.cursor->y -= 1;
		int x = ptr->length;
		for (int i = 0; i < temp->length; i++)
		{
			page.cursor->x = ptr->length;
			add_ch(temp->line[i]);
		}
		page.cursor->x = x;
		free(temp->line);
		free(temp);
	}
}

void delete ()
{
	Row *ptr = page.row;
	for (int i = 0; i < page.cursor->y; i++)
		ptr = ptr->next;
	//arada bir yerde ise
	if (page.cursor->x < ptr->length)
	{
		for (int i = page.cursor->x + 1; i <= ptr->length - 1; i++)
			ptr->line[i - 1] = ptr->line[i];

		ptr->line = (wchar *)realloc(ptr->line, sizeof(wchar) * (ptr->length));
		ptr->length -= 1;
	}
	//sonda ise, alt satırı getir, null ise getirme
	else if (ptr->next != NULL)
	{
		Row *temp = ptr->next;
		ptr->next = temp->next;
		int x = ptr->length;
		for (int i = 0; i < temp->length; i++)
		{
			page.cursor->x = ptr->length;
			add_ch(temp->line[i]);
		}
		page.cursor->x = x;
		free(temp->line);
		free(temp);
	}
}

int byte_number(unsigned int ch)
{
	if (ch <= 127)
		return 1;
	else if (ch <= 223)
		return 2;
	else if (ch <= 239)
		return 3;
	else if (ch <= 247)
		return 4;
	else
		return -1;
}

unsigned int unicode_to_utf8(unsigned int ch,int input_device)
{
	int bytes = byte_number(ch);
	unsigned int c = ch;
	for (int i = 1; i < bytes; i++)
	{
		if(input_device == input_device_stdin)
			ch = getch();
		else if(input_device == input_device_file)
			ch = fgetc(file.file_ptr);
		
		c += ch << (8 * i);
	}
	return c;
}
void save_file();
int process_key(unsigned int ch, int input_device)
{
	switch (ch)
	{
	case ALT_CTRL_Q:
	{
		endwin();
		exit(0);
	}
	case ALT_CTRL_S:
	{
		save_file();
		break;
	}
	case KEY_RIGHT:
	{
		Row *ptr = page.row;
		for (int i = 0; i < page.cursor->y; i++)
			ptr = ptr->next;

		if (page.cursor->x != ptr->length)
			page.cursor->x += 1;

		else if (ptr->next != NULL)
		{
			page.cursor->x = 0;
			page.cursor->y += 1;
		}
		move(page.cursor->y, page.cursor->x);
		page.cursor->lastX = -1;
		break;
	}
	case KEY_LEFT:
	{
		if (page.cursor->x > 0)
			page.cursor->x -= 1;

		else if (page.cursor->y > 0)
		{
			Row *ptr = page.row;
			for (int i = 0; i < page.cursor->y - 1; i++)
				ptr = ptr->next;

			page.cursor->x = ptr->length;
			page.cursor->y -= 1;
		}
		move(page.cursor->y, page.cursor->x);
		page.cursor->lastX = -1;
		break;
	}
	case KEY_UP:
	{
		if (page.cursor->y > 0)
		{
			page.cursor->y -= 1;
			if (page.cursor->y < page.screen_y)
				page.screen_y -= 1;
		}

		Row *ptr = page.row;
		for (int i = 0; i < page.cursor->y; i++)
			ptr = ptr->next;
		if (page.cursor->lastX != -1)
			page.cursor->x = page.cursor->lastX;

		if (ptr->length <= page.cursor->x)
		{
			page.cursor->lastX = page.cursor->x;
			page.cursor->x = ptr->length;
		}
		break;
	}
	case KEY_DOWN:
	{
		Row *ptr = page.row;
		for (int i = 0; i < page.cursor->y; i++)
			ptr = ptr->next;
		if (ptr->next != NULL)
		{
			page.cursor->y += 1;
			ptr = ptr->next;

			if (page.cursor->y >= page.screen_y + page.max_y)
				page.screen_y += 1;
		}
		else
			page.cursor->x = ptr->length;

		if (page.cursor->lastX != -1)
			page.cursor->x = page.cursor->lastX;

		if (ptr->length - 1 < page.cursor->x)
		{
			page.cursor->lastX = page.cursor->x;
			page.cursor->x = ptr->length;
		}
		break;
	}
	case ALT_BACKSPACE:
	{
		backspace();
		break;
	}
	case ALT_DELETE:
	{
		delete ();
		break;
	}
	case ALT_ENTER:
	{
		new_line(ch);
		page.cursor->lastX = -1;
		if (page.cursor->y >= page.screen_y + page.max_y)
			page.screen_y += 1;
		break;
	} //TOD tab tuşu boşluğa çevrildi mümküse tab olarak kalsın
	case ALT_TAB:
	{
		int tab_size = 8;
		for (int i = 0; i < tab_size; i++)
			add_ch(' ');
		page.cursor->x += tab_size;
		page.cursor->lastX = -1;
		break;
	}
	default:
	{
		ch = unicode_to_utf8(ch,input_device);
		add_ch(ch);
		page.cursor->x += 1;
		page.cursor->lastX = -1;
	}
	}
}

void print()
{
	char temp[5] = {0};
	int bytes = 0;

	move(0, 0);
	refresh();
	clear();
	Row *ptr = page.row;
	for (int i = 0; i < page.screen_y; i++)
		ptr = ptr->next;

	int printing_row_count = 0;
	int start_y = 0;
	int end_y = 0;

	while (ptr != NULL && printing_row_count < page.max_y)
	{
		end_y = page.max_x - 1 > ptr->length ? ptr->length : page.max_x - 1;
		start_y = page.cursor->x > page.max_x - 1 ? page.cursor->x - (page.max_x - 1) : 0;
		for (int i = start_y; i < end_y + start_y && i < ptr->length; i++)
		{
			for (int j = 0; j < 5; j++)
				temp[j] = 0;

			bytes = byte_number(ptr->line[i] & 255);
			for (int j = 0; j < bytes; j++)
				temp[j] = (ptr->line[i] >> (8 * j)) & 255;

			printw("%s", temp);
		}
		ptr = ptr->next;
		printw("\n");
		printing_row_count += 1;
	}
	page.screen_x = page.cursor->x > page.max_x - 1 ? page.max_x - 1 : page.cursor->x;
	move((page.cursor->y - page.screen_y), page.screen_x);
	refresh();
}


void read_file(const char *file_name)
{
	file.file_name = strdup(file_name);
	file.file_ptr = fopen(file.file_name, "r");

	struct stat st;
	if (stat(file_name, &st) == 0)
	{
		if (st.st_size == 0)
		{
			fclose(file.file_ptr);
			return;
		}
	}
	if (file.file_ptr != NULL)
	{
		unsigned int ch;
		ch = fgetc(file.file_ptr);
		while (ch != EOF)
		{
			process_key(ch,input_device_file);
			ch = fgetc(file.file_ptr);
		} 
		print();
		fclose(file.file_ptr);
	}
}

void save_file()
{
	file.file_ptr = fopen(file.file_name, "w");

	char temp[5] = {0};
	int bytes = 0;

	Row *ptr = page.row;
	int start_y = 0;
	int end_y = 0;

	while (ptr != NULL )
	{
		end_y = page.max_x - 1 > ptr->length ? ptr->length : page.max_x - 1;
		start_y = page.cursor->x > page.max_x - 1 ? page.cursor->x - (page.max_x - 1) : 0;
		for (int i = start_y; i < end_y + start_y && i < ptr->length; i++)
		{
			for (int j = 0; j < 5; j++)
				temp[j] = 0;

			bytes = byte_number(ptr->line[i] & 255);
			for (int j = 0; j < bytes; j++)
				temp[j] = (ptr->line[i] >> (8 * j)) & 255;

			fprintf(file.file_ptr, "%s", temp);
		}
		ptr = ptr->next;
		fprintf(file.file_ptr, "\n");
	}
	fclose(file.file_ptr);
}

void init_editor()
{
	page.cursor = (Cursor *)malloc(sizeof(Cursor));
	page.row = (Row *)malloc(sizeof(Row));

	page.row->line = (wchar *)malloc(sizeof(wchar));
	page.row->line[0] = '\0';
	page.row->next = NULL;
	page.row->length = 0;
	page.row_count = 1;

	page.cursor->y = 0;
	page.cursor->x = 0;
	page.cursor->lastX = -1;

	getmaxyx(stdscr, page.max_y, page.max_x);
	page.screen_x = page.max_x;
	page.screen_y = 0;
}

static void sighandler(int signum)
{
	if (SIGWINCH == signum)
	{
		struct winsize w;
		ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
		page.max_y = w.ws_row;
		page.max_x = w.ws_col;
		resizeterm(page.max_y, page.max_x);
		//page.screen_y = 0;
		print();
		//clear buffer
		while ((getch()) != 410 ); 
		
	}
}

int main(int argc, char const *argv[])
{
	if (argc < 2)
	{
		printf("please enter filename\n\n");
		return -1;
	}

	setlocale(LC_ALL, ""); //utf-8 dessteği
	initscr();
	raw();			  //tuşa basar basmaz eriş
	noecho();			  //bastığım tuş gözükmesin
	keypad(stdscr, true); //f ve ok tuşlarını etkinleştir

	init_editor();
	read_file(argv[1]);
	signal(SIGWINCH, sighandler);

	unsigned int ch;
	int n;
	while (1 == 1)
	{
		ch = getch();
		process_key(ch,input_device_stdin);
		if (ioctl(0, FIONREAD, &n) == 0 && n == 0)
			print();
	}
	endwin();
	return 0;
}
