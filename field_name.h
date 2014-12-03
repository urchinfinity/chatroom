#define ERR_EXIT(a) {perror(a); exit(1);}

#define DB_SERVER "localhost"
#define DB_USER "root"
#define DB_PWD "1234"
#define DB_DATABASE "chatroom"

#define COMMENT "cmt"
#define UPLOAD "upload"
#define DOWNLOAD "download"
#define HISTORY "history"
#define ONLINE "online"
#define SIGNUP "signup"
#define SIGNIN "signin"
#define NEWROOM "newroom"

#define UPLOAD_S "start"
#define UPLOAD_M "middle"
#define UPLOAD_E "end"
#define DOWNLOAD_S "sending"
#define DOWNLOAD_E "end"

#define L_REQ 3
#define LAST_REQ 2
#define CMD_SIZE 50

#define L_CMT_REQ 3 // [cmt, roomname, length, text]
#define L_UPLOAD_REQ 3
#define L_UPLOAD_REQ_S 3 // [upload, start, length, filename]
#define L_UPLOAD_REQ_M 3 // [upload, middle, length, text]
#define L_UPLOAD_REQ_E 2 // [upload, end, length, roomname]
#define L_DOWNLOAD_REQ 1 // [download, filename,]
#define L_HIS_REQ 1 // [history, roomname,]
#define L_ONLINE_REQ 1 // [online, roomname,]
#define L_SIGNUP_REQ 3 // [signup, username, nickname, pwd,]
#define L_SIGNIN_REQ 2 // [signin, username, pwd,]
#define L_NEWROOM_REQ 3 // [newroom, roomname, length, members(seperate by ','')]

#define ROOMNAME 0
#define UPLOAD_STATUS 0
#define FILENAME 2
#define UPLOAD_ROOMNAME 2
#define QUERYFILE 0
#define TEXT 2
#define USERNAME 0
#define NICKNAME 1
#define SIGNUP_PWD 2
#define SIGNIN_PWD 1
#define MEMBERS 2

#define L_SIGN_RES 3 // [success, username, nickname,]
					 // [fail,,,]
#define L_CMT_RES 4 // [cmt, roomname, nickname, length, text]
#define L_UPLOAD_RES 4 // [upload, roomname, username, nickname, filename,]
#define L_DOWNLOAD_RES 1
#define L_DOWNLOAD_RES_S 2 // [download, sending, length, text]
// no use
#define L_DOWNLOAD_RES_E 1 // [download, end,]

#define L_HISTORY 3 // [history, roomname, length, text]
#define L_ONLINE_RES 1 // [online, roomname, num, onlines, num, offlines]

#define C_ROOMNAME 0
#define C_CMT_NICK 1
#define C_CMT_TEXT 3
#define C_UPLOAD_USER 1
#define C_UPLOAD_NICK 2
#define C_UPLOAD_FILE 3
#define C_DOWNLOAD_STATUS 0
#define C_DOWNLOAD_TEXT 1
#define C_MEMBER_NUM 0
