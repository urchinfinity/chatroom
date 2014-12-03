#include <arpa/inet.h>
#include <curses.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include "field_name.h"
#include "client_gui.h"

typedef struct {
    int fd;
    int exist_file;
    char name[128];
    char content[1024];
} file;

typedef struct {
    char type[CMD_SIZE + 1];
    char buf[2014];
    int buf_len;

    char roomname[50];
    char username[50];
    char nickname[50];
    
    file *download;
} client;

int str_to_int(char *buf) {
    int i, value = 0, num;
    for (i = 0; buf[i] != '\0'; i++) {
        num = buf[i] - '0';
        value = value * 10 + num;
    }
    return value;
}

static void init_connection();
void init_user();
void signup_signin();
void *response_handler(void *arg);
void serve_response();
void show_comment();
void show_uploadfile();
void show_console();
void show_online_member(char *member);
void show_offline_member(char *member);
char **parse_response_and_text(int parse_len);
char **parse_response(int parse_len);
void *upload_handler(void *arg);
// void get_roomnames();
void get_online();
void init_curses();
void init_windows_attr();
void init_windows();
void init_inputfile(input_w *inputfile);
void init_inputbox(input_w *inputbox);
void init_roomnames();
void init_download();
WINDOW *create_titlebox(char *username);
WINDOW *create_infobox();
WINDOW *create_roomnames_box();
WINDOW *create_members_box();
WINDOW *create_room_border_box(char *roomname);
WINDOW *create_roombox();
WINDOW *create_consolebox();
WINDOW *create_filepath_inputbox();
WINDOW *create_upload_button();
WINDOW *create_inputbox();
void send_cmt();
void upload_file();
void get_filename(char *filepath, char *filename);
void reset_input();
int click_room(int x, int y);
int click_filepath(int x, int y);
int click_upload(int x, int y);
int click_inputbox(int x, int y);
int click_download(int x, int y);
int click_cancel(int x, int y);
int get_clicked_roomname(int x);
void update_roomname_win();
void focus_on_target(input_w *tar);
void serve_mouse_event(int x, int y);

void close_client();

int sock;
client *user;

roomname_w roomnames;
member_w members;
room_w room;
input_w *filepath, *inputbox, *target;
MEVENT event;

int main(int argc, char *argv[]) {

    if (argc != 2) {
        fprintf(stderr, "usage: %s [ip]\n", argv[0]);
        exit(1);
    }

    init_user();
    init_connection(argv[1]);
    signup_signin();

    init_curses();
    init_windows_attr();
    init_windows();
     
    // catch response from server
    pthread_t response_thd;
    if(pthread_create(&response_thd, NULL, response_handler, (void *)0))
        ERR_EXIT("Create response thread failed");

    get_online();

    while (1) {
        int c = wgetch(target->win);
        switch(c) {
            case '\t':
                target->text[target->len] = '\0';
                strcat(target->text, "    ");
                target->len += 4;
                target->y += 4;
                break;
            case KEY_BACKSPACE:
                if (target->y != 0) {
                    target->len--;
                    if (target->offset == 0)
                        mvwaddch(target->win, target->x,--target->y,' ');
                    else {
                        int i;
                        for (i = target->len + target->offset; i < target->len; i++) {
                            target->text[i] = target->text[i + 1];
                            mvwaddch(target->win, target->x, i, target->text[i]);
                        }
                        mvwaddch(target->win, target->x, i,' ');
                        target->y--;
                    }
                    wrefresh(target->win);
                }
                break;
            case 27: //esc
                close_client();
            case KEY_MOUSE:
                // waddstr(target->win, "clicked");
                if(getmouse(&event) == OK)
                    serve_mouse_event(event.y, event.x);
                break;
            case 13: //enter
                target->text[target->len] = '\0';
                if (strcmp(target->type, COMMENT) == 0) {
                    send_cmt();
                } else {
                    upload_file();
                }
                reset_input();
                break;
            case KEY_DOWN:
                break;
            case KEY_UP:
                break;
            case KEY_LEFT:
                if (target->len != 0) {
                    target->y--;
                    target->offset--;
                }
                break;
            case KEY_RIGHT:
                if (target->offset != 0) {
                    target->y++;
                    target->offset++;
                }
                break;
            default:
                mvwaddch(target->win, target->x, target->y, c);
                if (target->offset == 0) {
                    target->text[target->len++] = c;
                } else {
                    int i;
                    for (i = target->len; i > target->len + target->offset; i--)
                        target->text[i] = target->text[i - 1];
                    target->text[i] = c;
                    target->len++;
                    while (i++ < target->len - 1)
                        mvwaddch(target->win, target->x, i, target->text[i]);
                }
                wrefresh(target->win);
                // wprintw(target->win, "%d", c);
                target->y++;
                break;
        }
        wmove(target->win, target->x, target->y);
        wrefresh(target->win);
    }
    close_client();
}

static void init_connection(char *targetIP) {
    struct sockaddr_in server;

    //Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        ERR_EXIT("Could not create socket");
    // puts("Socket created");
     
    server.sin_addr.s_addr = inet_addr(targetIP);
    server.sin_family = AF_INET;
    server.sin_port = htons(8888);
 
    //Connect to remote server
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0)
        ERR_EXIT("connect failed. Error");
    // puts("Connected!");
}

void init_user() {
    user = (client *)malloc(sizeof(client));
    user->download = (file *)malloc(sizeof(file));
    user->download->exist_file = 0;
    strcpy(user->roomname, "public");
}

void signup_signin() {
    int i, authen = 0;
    char action[10], input[50], request[256];
    char **response;
    printf("signup/signin? ");
    scanf(" %s", action);

    while(!authen) {
        sprintf(request, "%s,", action);
        if (strcmp(action, "signup") == 0) {
            printf("[username] [nickname] [password]: ");
            for (i = 0; i < L_SIGNUP_REQ; i++) {
                scanf(" %s", input);
                strcat(request, input);
                strcat(request, ",");
            }
            getchar();

            if(send(sock, request, strlen(request) , 0) < 0)
                ERR_EXIT("Send failed");

            response = parse_response(L_SIGN_RES);
            if (strcmp(response[0], "success") == 0) {
                strcpy(user->username, response[1]);
                strcpy(user->nickname, response[2]);
                authen = 1;
            }
            else
                printf("%s\n", response[0]);
        } else if (strcmp(action, "signin") == 0) {
            printf("[username] [password]: ");
            for (i = 0; i < L_SIGNIN_REQ; i++) {
                scanf(" %s", input);
                strcat(request, input);
                strcat(request, ",");
            }
            getchar();

            if(send(sock, request, strlen(request) , 0) < 0)
                ERR_EXIT("Send failed");

            response = parse_response(L_SIGN_RES);
            if (strcmp(response[0], "success") == 0) {
                strcpy(user->username, response[1]);
                strcpy(user->nickname, response[2]);
                authen = 1;
            }
            else
                printf("%s\n", response[0]);
        }
    }
    free(response);
}

// void get_roomnames() {
// }

void get_online() {
    char request[64];
    sprintf(request,"%s,%s,", ONLINE, user->roomname);
    if(send(sock, request, strlen(request) , 0) < 0)
        ERR_EXIT("Send failed");
}

void *response_handler(void *arg) {
    int nfd;
    fd_set read_set;
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100;

    while (1) {
        FD_ZERO(&read_set);
        FD_SET(sock, &read_set);

        nfd = select(sock + 1, &read_set, NULL, NULL, &tv);
        if (nfd > 0) {
            int len, recvIsDone = 0;
            user->buf_len = 0;
            while (!recvIsDone) {
                if ((len = recv(sock, user->buf, 1, 0)) > 0) {
                    if(user->buf[0] == ',') {
                        user->type[user->buf_len++] = '\0';
                        // printf("%s\n", user->type);
                        serve_response();
                        recvIsDone = 1;
                    } else
                        user->type[user->buf_len++] = user->buf[0];
                } else if (len == 0) {
                    puts("disconnected");
                    close_client();
                } else if (len == -1) {
                    puts("recv failed");
                    close_client();
                }
            }    
        }
    }
}

void serve_response() {
    int i;
    char **response;

    if (strcmp(user->type, COMMENT) == 0) {
        response = parse_response_and_text(L_CMT_RES);
        if (strcmp(response[C_ROOMNAME], user->roomname) != 0)
            return;
        sprintf(user->buf, "%s:    %s\n", response[C_CMT_NICK], response[C_CMT_TEXT]);
        show_comment();
    } else if (strcmp(user->type, UPLOAD) == 0) {
        response = parse_response(L_UPLOAD_RES);
        if (strcmp(response[C_ROOMNAME], user->roomname) != 0)
            return;
        if (strcmp(response[C_UPLOAD_USER], user->username) == 0)
            return;
        user->download->exist_file = 1;
        strcpy(user->download->name, response[C_UPLOAD_FILE]);
        sprintf(user->buf, "%s uploads %s\n", response[C_UPLOAD_NICK], response[C_UPLOAD_FILE]);
        show_uploadfile();
    } else if (strcmp(user->type, DOWNLOAD) == 0) {
        response = parse_response(L_DOWNLOAD_RES);
        if (strcmp(response[C_DOWNLOAD_STATUS], DOWNLOAD_S) == 0) {
            response = parse_response_and_text(L_DOWNLOAD_RES_S);
            write(user->download->fd, response[C_DOWNLOAD_TEXT], strlen(response[C_DOWNLOAD_TEXT]));
        } else if (strcmp(response[C_DOWNLOAD_STATUS], DOWNLOAD_E) == 0) {
            close(user->download->fd);
            wclear(room.console);
            waddstr(room.console, "Finish downloading.");
            wrefresh(room.console);
        }
    } else if (strcmp(user->type, HISTORY) == 0) {

    } else if (strcmp(user->type, ONLINE) == 0) {
        wclear(members.win);
        response = parse_response(L_ONLINE_RES);
        if (strcmp(response[C_ROOMNAME], user->roomname) != 0)
            return;
        response = parse_response(L_ONLINE_RES);
        members.online_num = str_to_int(response[C_MEMBER_NUM]);
        response = parse_response(members.online_num);
        for (i = 0; i < members.online_num; i++)
            show_online_member(response[i]);

        response = parse_response(L_ONLINE_RES);
        members.offline_num = str_to_int(response[C_MEMBER_NUM]);
        response = parse_response(members.offline_num);
        for (i = 0; i < members.offline_num; i++)
            show_offline_member(response[i]);
    } else if (strcmp(user->type, NEWROOM) == 0) {

    } else {
        printf("Unknown response type: {%s}\n", user->type);
    }
    wmove(target->win, target->x, target->y);
    wrefresh(target->win);
    free(response);
}

void show_comment() {
    waddstr(room.win, user->buf);
    wrefresh(room.win);   
}

void show_uploadfile() {
    wattron(room.win, COLOR_PAIR(CYAN));
    show_comment();
    wattroff(room.win, COLOR_PAIR(CYAN));
    show_console();
}

void show_console() {
    int i;
    for (i = 0; i < room_col; i++)
        mvwaddch(room.console, 0, i, ' ');
    mvwprintw(room.console, 0, 0, "Download %s ?",user->download->name);
    wattron(room.console, A_REVERSE);
    mvwaddstr(room.console, 0, room_col - 6, CC_DOWNLOAD);
    mvwaddstr(room.console, 0, room_col - 2, CC_CALCEL);
    wattroff(room.console, A_REVERSE);
    wrefresh(room.console);
}


void show_online_member(char *member) {
    wprintw(members.win, ":) %s\n", member);
    wrefresh(members.win);
}

void show_offline_member(char *member) {
    wprintw(members.win, "X( %s\n", member);
    wrefresh(members.win);
}

char **parse_response_and_text(int parse_len) {
    // printf("in parser\n");
    int len, buf_len, i;
    char buf[1];
    char **data = (char **)malloc(parse_len * sizeof(char *));

    for (i = 0; i < parse_len; i++) {
        // printf("in parser loop\n");
        if (i == parse_len - 1) {
            // printf("in last loop\n");
            int text_len = str_to_int(data[i - 1]);
            data[i] = (char *)malloc((text_len + 1) * sizeof(char));
            if ((len = recv(sock, data[i], text_len, 0)) > 0) {
                data[i][len] = '\0';
                // printf("%s\n", data[i]);
            }
        } else {
            // printf("in prev loop\n");
            int recvIsDone = 0;
            buf_len = 0;
            data[i] = (char *)malloc((CMD_SIZE + 1) * sizeof(char));

            while (!recvIsDone) {
                if ((len = recv(sock, buf , 1, 0)) > 0) {
                    if(buf[0] == ',') {
                        data[i][buf_len++] = '\0';
                        // printf("%s\n", data[i]);
                        recvIsDone = 1;
                    } else
                        data[i][buf_len++] = buf[0];
                }
            }
        }
        if (len == 0) {
            ERR_EXIT("Server disconnected");
        } else if (len == -1)
            ERR_EXIT("recv1 failed");
    }
    // printf("out parser\n");
    return data;
}

char **parse_response(int parse_len) {
    int len, buf_len, i;
    char buf[1];
    char **data = (char **)malloc(parse_len * sizeof(char *));

    for (i = 0; i < parse_len; i++) {
        int recvIsDone = 0;
        buf_len = 0;
        data[i] = (char *)malloc((CMD_SIZE + 1) * sizeof(char));

        while (!recvIsDone) {
            if ((len = recv(sock, buf , 1, 0)) > 0) {
                if(buf[0] == ',') {
                    data[i][buf_len++] = '\0';
                    // printf("parse %s\n", data[i]);
                    recvIsDone = 1;
                } else
                    data[i][buf_len++] = buf[0];
            } else if (len == 0) {
                ERR_EXIT("Server disconnected");
            } else if (len == -1)
                ERR_EXIT("recv1 failed");
        }
    }
    return data;
}

void init_curses() {
    initscr();
    start_color();
    use_default_colors();
    init_pair(YELLOW, COLOR_YELLOW, -1);
    init_pair(GREEN, COLOR_GREEN, -1);
    init_pair(RED, COLOR_RED, -1);
    init_pair(BLUE, COLOR_BLUE, -1);
    init_pair(CYAN, COLOR_CYAN, -1);
    init_pair(MAGENTA, COLOR_MAGENTA, -1);
    init_pair(WHITE_BLUE, COLOR_WHITE, COLOR_BLUE);
    cbreak();
    nonl();
    noecho();
    intrflush(stdscr,FALSE);
    refresh();
}

void init_windows_attr() {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    t_rows = w.ws_row;
    t_cols = w.ws_col;

    title_col = t_cols;
    info_row = t_rows - TITLE_ROW;
    info_y = t_cols - INFO_COL;
    roomnames_y = t_cols - INFO_COL + 1;
    members_row = info_row - ROOMNAMES_ROW - 7;
    members_y = t_cols - INFO_COL + 1;
    room_border_row = t_rows - TITLE_ROW - FILEPATH_ROW - INPUTBOX_ROW;
    room_border_col = t_cols - INFO_COL;
    room_row = room_border_row - 6;
    room_col = room_border_col - 2;
    console_x = TITLE_ROW + room_border_row - 2;
    fileprefix_x = t_rows - INPUTBOX_ROW - FILEPATH_ROW + 1;
    filepath_col = t_cols - strlen(FILEPREFIX) - UPLOADB_COL - INFO_COL;
    filepath_x = t_rows - FILEPATH_ROW - INPUTBOX_ROW;
    uploadb_x = t_rows - INPUTBOX_ROW - FILEPATH_ROW;
    uploadb_y = t_cols - strlen(UPLOADB) - INFO_COL;
    inputbox_col = t_cols - INFO_COL;
    inputbox_x = t_rows - INPUTBOX_ROW;
}

void init_windows() {
    filepath = (input_w *)malloc(sizeof(input_w));
    inputbox = (input_w *)malloc(sizeof(input_w));
    init_inputfile(filepath);
    init_inputbox(inputbox);

    create_titlebox(user->username);
    create_infobox();
    roomnames.win = create_roomnames_box();
    members.win = create_members_box();
    room.border = create_room_border_box(user->roomname);
    room.win = create_roombox();
    room.console = create_consolebox();
    filepath->win = create_filepath_inputbox();
    create_upload_button();
    inputbox->win = create_inputbox();
    
    init_roomnames();

    scrollok(room.win, TRUE);
    keypad(inputbox->win,TRUE);
    keypad(filepath->win,TRUE);
    mousemask(ALL_MOUSE_EVENTS, NULL);

    target = inputbox;
    wmove(target->win, target->x, target->y);
    wrefresh(target->win);
}

void init_inputfile(input_w *inputfile) {
    inputfile->start_x = 0;
    inputfile->x = 0;
    inputfile->start_y = 0;
    inputfile->y = 0;
    inputfile->len = 0;
    inputfile->offset = 0;
    inputfile->text = (char *)malloc(64 * sizeof(char));
    strcpy(inputfile->type, UPLOAD);
}

void init_inputbox(input_w *inputbox) {
    inputbox->start_x = 0;
    inputbox->x = 0;
    inputbox->start_y = 0;
    inputbox->y = 0;
    inputbox->len = 0;
    inputbox->offset = 0;
    inputbox->text = (char *)malloc(512 * sizeof(char));
    strcpy(inputbox->type, COMMENT);
}

WINDOW *create_titlebox(char *username) {
    WINDOW *title = newwin(TITLE_ROW, title_col, TITLE_X, TITLE_Y);
    wattron(title, A_REVERSE);
    int i, j;
    for (i = 0;i < TITLE_ROW; i++)
        for (j = 0; j < title_col; j++)
            mvwaddch(title, i, j, ' ');

    wattron(title, A_BOLD);
    char titletext[100];
    strcpy(titletext, username);
    strcat(titletext, TITLE);
    mvwaddstr(title, 1, (title_col - strlen(titletext)) / 2, titletext);
    wattroff(title, A_BOLD);

    wattroff(title, A_REVERSE);
    wrefresh(title);
    return title;
}

WINDOW *create_infobox() {
    WINDOW *info = newwin(info_row, INFO_COL, INFO_X, info_y);
    wattron(info, COLOR_PAIR(GREEN));
    box(info, '|', '-');
    int i, j;
    for (i = 0; i < 2; i++) {
        for (j = 1; j < INFO_COL - 1; j++) {
            mvwaddch(info, i * INFO_SPACE, j, '-');
            mvwaddch(info, i * INFO_SPACE + 2, j, '-');
        }
    }
    mvwaddstr(info, 1, INFO_1_Y, INFO_1);
    mvwaddstr(info, INFO_SPACE + 1, INFO_2_Y, INFO_2);
    wattroff(info, COLOR_PAIR(GREEN));
    wrefresh(info);
    return info;
}

WINDOW *create_roomnames_box() {
    return newwin(ROOMNAMES_ROW, ROOMNAMES_COL, ROOMNAMES_X, roomnames_y);
}

WINDOW *create_members_box() {
    return newwin(members_row, MEMBERS_COL, MEMBERS_X, members_y);
}

WINDOW *create_room_border_box(char *roomname) {
    WINDOW *room_border = newwin(room_border_row, room_border_col, ROOM_BORDER_X, ROOM_BORDER_Y);
    wattron(room_border, COLOR_PAIR(YELLOW));
    wattron(room_border, A_BOLD);
    box(room_border, '|', '-');
    int i;
    for (i = 1; i < room_border_col - 1; i++) {
        mvwaddch(room_border, 2, i, '-');
        mvwaddch(room_border, room_border_row - 3, i, '-');
    }
    mvwaddstr(room_border, 1, (room_col - strlen(roomname)) / 2, roomname);
    wattroff(room_border, A_BOLD);
    wattroff(room_border, COLOR_PAIR(YELLOW));
    wrefresh(room_border);
    return room_border;
}

WINDOW *create_roombox() {
    WINDOW *room = newwin(room_row, room_col, ROOM_X, ROOM_Y);
    return room; 
}

WINDOW *create_consolebox() {
    WINDOW *console = newwin(1, room_col, console_x, 1);
    return console; 
}

WINDOW *create_filepath_inputbox() {
    attron(COLOR_PAIR(BLUE));
    mvwaddstr(stdscr, fileprefix_x, FILEPREFIX_Y, FILEPREFIX);
    attroff(COLOR_PAIR(BLUE));
    wrefresh(stdscr);

    WINDOW *filepathb = newwin(FILEPATH_ROW, filepath_col, filepath_x, FILEPATH_Y);
    wattron(filepathb, COLOR_PAIR(BLUE));
    box(filepathb, '|', '-');
    wattroff(filepathb, COLOR_PAIR(BLUE));
    wrefresh(filepathb);

    WINDOW *filepath = newwin(1, filepath_col - 2, filepath_x + 1, FILEPATH_Y + 1);
    return filepath; 
}

WINDOW *create_upload_button() {
    WINDOW *uploadb = newwin(UPLOADB_ROW, UPLOADB_COL, uploadb_x, uploadb_y);
    wattron(uploadb, COLOR_PAIR(WHITE_BLUE));
    mvwaddstr(uploadb, 0, 0, "        ");
    mvwaddstr(uploadb, 1, 0, UPLOADB);
    mvwaddstr(uploadb, 2, 0, "        ");
    wattroff(uploadb, COLOR_PAIR(WHITE_BLUE));
    wrefresh(uploadb);
    return uploadb; 
}

WINDOW *create_inputbox() {
    WINDOW *inputborder = newwin(INPUTBOX_ROW, inputbox_col, inputbox_x, INPUTBOX_Y);
    box(inputborder, '|', '-');
    mvwaddch(inputborder, 1, 1, '>');
    wrefresh(inputborder);
    WINDOW *inputbox = newwin(INPUTBOX_ROW - 2, inputbox_col - 3, inputbox_x + 1, INPUTBOX_Y + 2);
    return inputbox; 
}

void init_roomnames() {
    roomnames.num = 0;
    roomnames.target = 0;
    strcpy(roomnames.name[roomnames.num++], user->roomname);
    update_roomname_win();
}

void send_cmt() {
    wrefresh(target->win);
    char request[1024];
    sprintf(request, "cmt,public,%d,%s", (int)strlen(target->text), target->text);
    if(send(sock, request, strlen(request), 0) < 0)
        ERR_EXIT("Send failed");
}

void upload_file() {
    pthread_t upload_thd;
    if(pthread_create(&upload_thd, NULL, upload_handler, (void *)target->text))
        ERR_EXIT("Create listener thread failed");
}

void *upload_handler(void *arg) {
    int fd, ret;
    char *filepath = (char *)arg;
    char buf[1001], request[1024];
    char *filename = (char *)malloc(128 * sizeof(char));
    
    get_filename(filepath, filename);

    fd = open(filepath, O_RDONLY);
    sprintf(request, "upload,start,%d,%s", (int)strlen(filename), filename);
    // printf("%s\n", request);
    if(send(sock, request, strlen(request), 0) < 0)
        ERR_EXIT("Send failed");
    while((ret = read(fd, buf, 1000)) > 0) {
        buf[ret] = '\0';
        sprintf(request, "upload,middle,%d,%s", (int)strlen(buf), buf);
        // printf("%s\n<--", request);
        if(send(sock, request, strlen(request), 0) < 0)
            ERR_EXIT("Send failed");
    }
    sprintf(request, "upload,end,6,public");
    // printf("%s %d\n", request, (int)strlen(request));
    if(send(sock, request, strlen(request), 0) < 0)
        ERR_EXIT("Send failed");
    close(fd);
    // puts("finish sending");
    
    free(filename);
}

void get_filename(char *filepath, char *filename) {
    int i, len = strlen(filepath);

    while (len-- > 0 && (filepath[len] != '\\' && filepath[len] != '/'));
    if (len != 0)
        len++;

    for (i = len; i < strlen(filepath); i++)
        filename[i - len] = filepath[i];
    filename[i - len] = '\0';
}

void reset_input() {
    target->x = target->start_x;
    target->y = target->start_y;
    target->len = 0;
    target->offset = 0;
    wclear(target->win);
    wrefresh(target->win);
}

int click_room(int x, int y) {
    if (x >= ROOMNAMES_X && x < ROOMNAMES_X + roomnames.num)
        if (y >= roomnames_y && y < roomnames_y + ROOMNAMES_COL)
            return 1;
    return 0;
}

int click_filepath(int x, int y) {
    if (x >= filepath_x && x < filepath_x + FILEPATH_ROW)
        if (y >= FILEPATH_Y && y < FILEPATH_Y + filepath_col)
            return 1;
    return 0;
}

int click_upload(int x, int y) {
    if (x >= uploadb_x && x < uploadb_x + UPLOADB_ROW)
        if (y >= uploadb_y && y < uploadb_y + UPLOADB_COL)
            return 1;
    return 0;
}

int click_inputbox(int x, int y) {
    if (x >= inputbox_x && x < inputbox_x + INPUTBOX_ROW)
        if (y >= INPUTBOX_Y && y < INPUTBOX_Y + inputbox_col)
            return 1;
    return 0;
}

int click_download(int x, int y) {
    if (x == console_x)
        if (y == info_y - 5 || y == info_y - 6 || y == info_y - 7)
            return 1;
    return 0;
}

int click_cancel(int x, int y) {
    if (x == console_x)
        if (y == info_y - 2 || y == info_y - 3)
            return 1;
    return 0;
}

int get_clicked_roomname(int x) {
    return x - ROOMNAMES_X;
}

void update_roomname_win() {
    wclear(roomnames.win);
    wmove(roomnames.win, 0, 0);
    int i;
    for (i = 0; i < roomnames.num; i++) {
        if (i == roomnames.target)
            wattron(roomnames.win, A_BOLD);
        wprintw(roomnames.win, "%s\n", roomnames.name[i]);
        if (i == roomnames.target)
            wattroff(roomnames.win, A_BOLD);
    }
    wrefresh(roomnames.win);
}

void focus_on_target(input_w *tar) {
    target = tar;
}

void serve_mouse_event(int x, int y) {
    if (click_room(x, y)) {
        // roomnames.target = get_clicked_roomname(x);
        // update_roomname_win();
        get_online();
        // query_history();
        // query_members();
        // update_chatroom();
    } else if (click_filepath(x, y)) {
        focus_on_target(filepath);
    } else if (click_upload(x, y)) {
        upload_file();
        reset_input();
    } else if (click_inputbox(x, y)) {
        focus_on_target(inputbox);
    } else if (user->download->exist_file) {
        if (click_download(x, y)) {
            wclear(room.console);
            waddstr(room.console, "downloading file...");
            wrefresh(room.console);
            user->download->exist_file = 0;

            init_download();
        } else if (click_cancel(x, y)) {
            wclear(room.console);
            wrefresh(room.console);
            user->download->exist_file = 0;
        }
    }
}

void init_download() {
    char filepath[128];
    sprintf(filepath, "download/%s", user->download->name);
    user->download->fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC,
        S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

    char request[1024];
    sprintf(request, "%s,%s,",DOWNLOAD, user->download->name);
    if(send(sock, request, (int)strlen(request) , 0) < 0)
        ERR_EXIT("Send failed");
}

void close_client() {
    free(user);
    free(room.comment);
    free(filepath);
    free(inputbox);
    endwin();
    close(sock);
    exit(0);
}