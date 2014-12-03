#define TITLE "'s chatroom"
#define INFO_1 "chatrooms"
#define INFO_2 "members"
#define FILEPREFIX " filepath:"
#define UPLOADB " upload "
#define CC_DOWNLOAD "yes"
#define CC_CALCEL "no"

#define TITLE_ROW 3
#define TITLE_X 0
#define TITLE_Y 0

#define INFO_COL 40
#define INFO_X 3
#define INFO_1_Y 16
#define INFO_2_Y 17
#define INFO_SPACE 13
#define ROOMNAMES_ROW 10
#define ROOMNAMES_COL 38
#define ROOMNAMES_X 6
#define MEMBERS_COL 38
#define MEMBERS_X 19

#define ROOM_BORDER_X 3
#define ROOM_BORDER_Y 0
#define ROOM_X 6
#define ROOM_Y 1

#define FILEPREFIX_Y 0
#define FILEPATH_ROW 3
#define FILEPATH_Y strlen(FILEPREFIX)

#define UPLOADB_ROW 3
#define UPLOADB_COL strlen(UPLOADB)

#define INPUTBOX_ROW 4
#define INPUTBOX_Y 0

#define YELLOW 1
#define GREEN 2
#define RED 3
#define BLUE 4
#define CYAN 5
#define MAGENTA 6
#define WHITE_BLUE 7

typedef struct {
	WINDOW *win;
	char name[ROOMNAMES_ROW][50];
	int num;
	int target;
} roomname_w;

typedef struct {
	WINDOW *win;
	int online_num;
	int offline_num;
} member_w;

typedef struct {
	WINDOW *border;
	WINDOW *win;
	WINDOW *console;
	char **comment;
	char filename[50];
	int num;
} room_w;

typedef struct {
	int start_x;
	int start_y;
	int x;
	int y;
	WINDOW *win;

	char *text;
	char type[10];
	int len;
	int offset;
} input_w;

int t_rows, t_cols;
int title_col, info_row, info_y, roomnames_y, members_row, members_y,
    room_border_row, room_border_col, room_row, room_col, console_x,
    fileprefix_x, filepath_col, filepath_x,
    uploadb_x, uploadb_y, inputbox_col, inputbox_x;