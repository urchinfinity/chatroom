#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <mysql/mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "field_name.h"

typedef struct {
    char username[CMD_SIZE + 1];
    char nickname[CMD_SIZE + 1];
    char pwd[20];
} user;

typedef struct {
    int conn_fd;
    int file_fd;
    int authen;
    int connected;
    pthread_t file_transfer_thd;

    char type[CMD_SIZE + 1];
    char buf[1024];
    char filename[128];
    char queryfile[128];
    size_t buf_len;
    user *usr;
} request;

typedef struct {
	char ***row;
	int size;
	int fields;
} sqlr;

typedef struct {
	time_t created_at;
	char text[1024];
	user created_by;
} comment;

typedef struct {
	char roomname[CMD_SIZE + 1];
	user user;
} authen;

int str_to_int(char *buf) {
    int i, value = 0, num;
    for (i = 0; buf[i] != '\0'; i++) {
        num = buf[i] - '0';
        value = value * 10 + num;
    }
    return value;
}

static void init_server();
static void init_db_connection();
static void init_request(request *reqP);
static void free_request(request *reqP);
void update_conn_fd(char *username, int fd);
void serve_request(request* reqP);
char **parse_request_and_text(request *reqP);
char **parse_request(int len, request *reqP);
sqlr *get_user_by_roomname(char *roomname);
sqlr *query_agent(char *query, char *condition);
sqlr *get_online_users();
sqlr *get_offline_users();
sqlr *get_online_users_by_roomname(char *roomname);
sqlr *get_offline_users_by_roomname(char *roomname);
sqlr *get_userfd(sqlr *name);
void *transfer_file(void *arg);

request *requestP = NULL;
int svrfd, maxfd;
MYSQL *conn;
MYSQL_RES *res;

int main() {
	int client_sock, clilen, i;
    struct sockaddr_in client;

    maxfd = getdtablesize();
   	clilen = sizeof(struct sockaddr_in);

    init_server();
	init_db_connection();
   
    if ((requestP = (request *)malloc(sizeof(request) * maxfd)) == NULL)
        ERR_EXIT("out of memory allocating all requests");

    for (i = 0; i < maxfd; i++)
        init_request(&requestP[i]);

    int nfds;
    fd_set read_set, write_set;
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100;

    //accept connection from an incoming client
    while (1) {
	    FD_ZERO(&read_set);
        FD_ZERO(&write_set);
        FD_SET(svrfd, &read_set);
        for (i = 0; i < maxfd; i++)
            if (requestP[i].connected == 1)
               FD_SET(requestP[i].conn_fd, &read_set);

        nfds = select(maxfd, &read_set, NULL, NULL, &tv);
        if (nfds > 0) {
        	if (FD_ISSET(svrfd, &read_set)) {
	            if ((client_sock = accept(svrfd, (struct sockaddr *)&client, (socklen_t*)&clilen)) < 0) {
		        	perror("accept failed");
	                continue;
	            }
	            puts("Connection accepted");
	            requestP[client_sock].conn_fd = client_sock;
	            requestP[client_sock].connected = 1;
	            fprintf(stderr, "getting a new request... fd %d \n", client_sock);
	            nfds--;
	        }
	        for (i = 4; i < maxfd && nfds > 0; i++) {
                if (FD_ISSET(i, &read_set)) {
                	int len, recvIsDone = 0;
                	requestP[i].buf_len = 0;
                	while (!recvIsDone) {
	                	if ((len = recv(i, requestP[i].buf , 1, 0)) > 0) {
	                		if(requestP[i].buf[0] == ',') {
	                			requestP[i].type[requestP[i].buf_len++] = '\0';
		        				printf("%s\n", requestP[i].type);
		        				serve_request(&requestP[i]);
		        				recvIsDone = 1;
	                		} else
	                			requestP[i].type[requestP[i].buf_len++] = requestP[i].buf[0];
	                	} else if (len == 0) {
		        			recvIsDone = 1;
						    puts("Client disconnected");
						    fflush(stdout);

	                        free_request(&requestP[i]);
						} else if (len == -1) {
		        			recvIsDone = 1;
						    perror("recv0 failed");
						}
					}
					nfds--;
				}
            }
        }
    }
    free(requestP);
	mysql_free_result(res);
    return 0;
}

static void init_server() {
	int nNetTimeout = 1000;
    struct sockaddr_in server;
     
    //Create socket
    svrfd = socket(AF_INET, SOCK_STREAM, 0);
    if (svrfd == -1)
        ERR_EXIT("Could not create socket");
    puts("Socket created");
     
    //Prepare the sockaddr_in structure
    bzero(&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(8888);
    
    //Set socket
    if (setsockopt(svrfd, SOL_SOCKET, SO_REUSEADDR, (void *)&nNetTimeout, sizeof(int)) < 0)
        ERR_EXIT("Set socket error");

    //Bind
    if (bind(svrfd,(struct sockaddr *)&server , sizeof(server)) < 0)
        ERR_EXIT("bind failed. Error");
    puts("bind done");
    
    //Listen for new connections
    if (listen(svrfd , 1024) < 0)
    	ERR_EXIT("listen error.");
    puts("Waiting for incoming connections...");
}

static void init_db_connection() {
	conn = mysql_init(NULL);
	if (!mysql_real_connect(conn, DB_SERVER, DB_USER, DB_PWD, DB_DATABASE, 0, NULL, 0)) {
		printf("Error connecting to database: %s\n", mysql_error(conn));
		exit(1);
	} else
		printf("Connected....\n");
}

static void init_request(request* reqP) {
    reqP->conn_fd = -1;
    reqP->connected = 0;
    reqP->authen = 0;
    reqP->buf_len = 0;
    reqP->usr = (user *)malloc(sizeof(user));
}

static void free_request(request* reqP) {
	if (reqP->authen == 1)
		update_conn_fd(reqP->usr->username, -1);
	close(reqP->conn_fd);
	free(reqP->usr);
    init_request(reqP);
}

void update_conn_fd(char *username, int fd) {
	char query[256];
	sprintf(query, "update Users set conn_fd = %d where username = '%s'",fd, username);
		if (mysql_query(conn, query))
			printf("Error making query: %s\n", mysql_error(conn));
}

void serve_request(request* reqP) {
	int i, j;
	char **request;
	char query[256];
	char response[1024];
	sqlr *result;

	if (strcmp(reqP->type, COMMENT) == 0) {
		request = parse_request_and_text(reqP);
		sprintf(response, "%s,%s,%s,%d,%s", COMMENT, request[ROOMNAME],reqP->usr->nickname, (int)strlen(request[TEXT]), request[TEXT]);

		if (strcmp(request[ROOMNAME], "public") == 0) {
			for (i = 0; i < maxfd; i++)
		    	if (requestP[i].connected == 1)
		    		send(i, response, (int)strlen(response), 0);
		} else {
			result = get_user_by_roomname(request[ROOMNAME]);
			result = get_userfd(result);

		    for (i = 0; i < result->size; i++) {
		    	int tar_fd = str_to_int(result->row[i][0]);
		    	if (tar_fd > 2 && requestP[tar_fd].connected == 1)
		    		send(tar_fd, response, (int)strlen(response), 0);
		    }
		}
		// sprintf(query, "insert into Comments(created_by, roomname, text) values('%s', '%s', '%s')",
		// 		reqP->usr->username, request[ROOMNAME], request[TEXT]);
		// 	 if (mysql_query(conn, query))
  //     			printf("Error making query: %s\n", mysql_error(conn));
	} else if (strcmp(reqP->type, UPLOAD) == 0) {
		// puts("------------>in upload");
		request = parse_request_and_text(reqP);
		if (strcmp(request[UPLOAD_STATUS], UPLOAD_S) == 0) {
			// printf("starting %s\n", request[2]);
			char filepath[128];
			strcpy(reqP->filename, request[FILENAME]);
			sprintf(filepath, "files/%s", request[FILENAME]);
			reqP->file_fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC,
                            S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
		} else if (strcmp(request[UPLOAD_STATUS], UPLOAD_M) == 0) {
			// printf("sending %s<---\n", request[2]);
			write(reqP->file_fd, request[TEXT], strlen(request[TEXT]));
		} else if (strcmp(request[UPLOAD_STATUS], UPLOAD_E) == 0) {
			// printf("ending %s\n", request[2]);
			close(reqP->file_fd);

			//send to roommate
			printf("target chatroom: %s", request[UPLOAD_ROOMNAME]);
			sprintf(response, "%s,%s,%s,%s,%s,", UPLOAD, request[UPLOAD_ROOMNAME], reqP->usr->username, reqP->usr->nickname, reqP->filename);
			if (strcmp(request[UPLOAD_ROOMNAME], "public") == 0) {
				for (i = 0; i < maxfd; i++)
			    	if (requestP[i].connected == 1)
			    		send(i, response, (int)strlen(response), 0);
			} else {
				result = get_user_by_roomname(request[UPLOAD_ROOMNAME]);
				result = get_userfd(result);

			    for (i = 0; i < result->size; i++) {
			    	int tar_fd = str_to_int(result->row[i][0]);
			    	if (tar_fd > 2 && requestP[tar_fd].connected == 1)
			    		send(tar_fd, response, (int)strlen(response), 0);
			    }
			}
		} 
	} else if (strcmp(reqP->type, DOWNLOAD) == 0) {
		request = parse_request(L_DOWNLOAD_REQ, reqP);
		strcpy(reqP->queryfile, request[QUERYFILE]);
		if(pthread_create(&((reqP->file_transfer_thd)), NULL, transfer_file, reqP))
        	ERR_EXIT("Create response thread failed");
	} else if (strcmp(reqP->type, HISTORY) == 0) {
		request = parse_request(L_HIS_REQ, reqP);
		
	} else if (strcmp(reqP->type, ONLINE) == 0) {
		request = parse_request(L_ONLINE_REQ, reqP);
		sprintf(response, "%s,%s,", ONLINE, request[ROOMNAME]);

		if (strcmp(request[ROOMNAME], "public") == 0) {
			result = get_online_users();
			sprintf(response, "%s%d,", response, result->size);
			for (i = 0; i < result->size; i++) {
				strcat(response, result->row[i][0]);
				strcat(response, ",");
			}
			result = get_offline_users();
			sprintf(response, "%s%d,", response, result->size);
			for (i = 0; i < result->size; i++) {
				strcat(response, result->row[i][0]);
				strcat(response, ",");
			}
			printf("onlines: %s\n", response);
			send(reqP->conn_fd, response, (int)strlen(response), 0);
		} else {
			result = get_online_users_by_roomname(request[ROOMNAME]);
			result = get_offline_users_by_roomname(request[ROOMNAME]);
		}
	} else if (strcmp(reqP->type, SIGNUP) == 0) {
		request = parse_request(L_SIGNUP_REQ, reqP);
		sprintf(query, "from Users where username = '%s'",	request[USERNAME]);
		int exist = query_count(query);
		if (!exist) {
			sprintf(query, "insert into Users values('%s', '%s', '%s', %d)",
				request[USERNAME], request[NICKNAME], request[SIGNUP_PWD], reqP->conn_fd);
			 if (mysql_query(conn, query))
      			printf("Error making query: %s\n", mysql_error(conn));
      		strcpy(reqP->usr->username, request[USERNAME]);
			strcpy(reqP->usr->nickname, request[NICKNAME]);
			reqP->authen = 1;

			sprintf(response, "success,%s,%s,", reqP->usr->username, reqP->usr->nickname);
			//select history of public chatroom
	    	send(reqP->conn_fd, response, strlen(response), 0);
		} else {
			strcpy(response, "username already used, , ,");
			send(reqP->conn_fd, response, strlen(response), 0);
		}
	} else if (strcmp(reqP->type, SIGNIN) == 0) {
		request = parse_request(L_SIGNIN_REQ, reqP);
		sprintf(query, "from Users where username = '%s'",	request[USERNAME]);
		int exist = query_count(query);
		if (!exist) {
			strcpy(response, "user not exist, , ,");
			send(reqP->conn_fd, response, strlen(response), 0);
		} else {
			sprintf(query, "select pwd, nickname from Users where username = '%s'", request[USERNAME]);
			if (mysql_query(conn, query))
      			printf("Error making query: %s\n", mysql_error(conn));
      		if ((res = mysql_use_result(conn))) {
				MYSQL_ROW row = mysql_fetch_row(res);
				if (strcmp(row[0], request[SIGNIN_PWD]) != 0) {		
					strcpy(response, "incorrect password, , ,");
					send(reqP->conn_fd, response, strlen(response), 0);
				} else {
					strcpy(reqP->usr->username, request[USERNAME]);
					strcpy(reqP->usr->nickname, row[1]);
					reqP->authen = 1;

					mysql_free_result(res);
					update_conn_fd(reqP->usr->username, reqP->conn_fd);
					sprintf(response, "success,%s,%s,", reqP->usr->username, reqP->usr->nickname);
					//select history of all chatrooms
	    			send(reqP->conn_fd, response, strlen(response), 0);
				}
			}
		}
	} else if (strcmp(reqP->type, NEWROOM) == 0) {
		request = parse_request_and_text(reqP);
		
	} else 
		printf("Unknown request type: {%s}\n", reqP->type);
	free(request);
}


char **parse_request_and_text(request *reqP) {
	// printf("in parser\n");
	int len, i;
	char **data = (char **)malloc(L_REQ * sizeof(char *));

	for (i = 0; i < L_REQ; i++) {
		// printf("in parser loop\n");
		if (i == LAST_REQ) {
			// printf("in last loop\n");
			int text_len = str_to_int(data[i - 1]);
			data[i] = (char *)malloc((text_len + 1) * sizeof(char));
			if ((len = recv(reqP->conn_fd, data[i], text_len, 0)) > 0) {
				data[i][len] = '\0';
				printf("%s\n", data[i]);
			}
		} else {
			// printf("in prev loop\n");
			int recvIsDone = 0;
			reqP->buf_len = 0;
			data[i] = (char *)malloc((CMD_SIZE + 1) * sizeof(char));

			while (!recvIsDone) {
				if ((len = recv(reqP->conn_fd, reqP->buf , 1, 0)) > 0) {
					if(reqP->buf[0] == ',') {
						data[i][reqP->buf_len++] = '\0';
						printf("%s\n", data[i]);
						recvIsDone = 1;
					} else
						data[i][reqP->buf_len++] = reqP->buf[0];
				}
			}
		}
		if (len == 0) {
			puts("Client disconnected");
			fflush(stdout);

			char query[256];
			sprintf(query, "update Users set conn_fd = -1 where username = '%s'", (requestP[i].usr)->username);
			if (mysql_query(conn, query))
				printf("Error making query: %s\n", mysql_error(conn));
			free_request(&requestP[i]);
		} else if (len == -1)
			perror("recv1 failed");
	}
	// printf("out parser\n");
	return data;
}

char **parse_request(int parse_len, request *reqP) {
	int i, len;
	char **data = (char **)malloc(parse_len * sizeof(char *));

	for (i = 0; i < parse_len; i++) {
		int recvIsDone = 0;
		reqP->buf_len = 0;
		data[i] = (char *)malloc((CMD_SIZE + 1) * sizeof(char));

		while (!recvIsDone) {
			if ((len = recv(reqP->conn_fd, reqP->buf , 1, 0)) > 0) {
				if(reqP->buf[0] == ',') {
					data[i][reqP->buf_len++] = '\0';
					printf("parse %s\n", data[i]);
					recvIsDone = 1;
				} else
					data[i][reqP->buf_len++] = reqP->buf[0];
			} else if (len == 0) {
				puts("Client disconnected");
				fflush(stdout);

				char query[256];
				sprintf(query, "update Users set conn_fd = -1 where username = '%s'", (requestP[i].usr)->username);
				if (mysql_query(conn, query))
					printf("Error making query: %s\n", mysql_error(conn));
				free_request(&requestP[i]);
				return NULL;
			} else if (len == -1) {
				perror("recv2 failed");
				return NULL;
			}
		}
	}
	return data;
}

int query_count(char *condition) {
	int t, count;
	char query_count[256];
	sprintf(query_count, "select count(*) %s", condition);

	if ((t = mysql_query(conn, query_count)))
	printf("Error making query: %s\n", mysql_error(conn));
	else {
		printf("Query count made...\n");
		if ((res = mysql_use_result(conn))) {
			MYSQL_ROW row = mysql_fetch_row(res);
			printf("# of rows: %s\n", row[0]);
			count = str_to_int(row[0]);
		}
	}
	mysql_free_result(res);
	return count;
}

sqlr *query_agent(char *query, char *condition) {
	int t, r;
	sqlr *result = (sqlr *)malloc(sizeof(sqlr));
	result->size = query_count(condition);

	if ((t = mysql_query(conn, query)))
		printf("Error making query: %s\n", mysql_error(conn));
	else {
		printf("Query made...\n");
		if ((res = mysql_use_result(conn))) {
			result->fields = mysql_num_fields(res);
			result->row = (char ***)malloc(result->size * sizeof(char **));
			for (r = 0; r < result->size; r++) {
				MYSQL_ROW row;
				row = mysql_fetch_row(res);
				result->row[r] = (char **)malloc(result->fields * sizeof(char *));
				for (t = 0; t < result->fields; t++) {
					result->row[r][t] = (char *)malloc((int)strlen(row[t]) * sizeof(char));
					strcpy(result->row[r][t], row[t]);
				}
			}
		}
	}
	mysql_free_result(res);
	return result;
}

sqlr *get_user_by_roomname(char *roomname) {
	char query[256];
	char condition[256];
	sqlr *result;

	sprintf(condition, "from Authen where roomname = '%s'", roomname);
	sprintf(query, "select username %s", condition);
	printf("query: %s %s\n", query, condition);

	result = query_agent(query, condition);
	return result;
}

sqlr *get_online_users() {
	char query[256];
	char condition[256];
	sqlr *result;

	strcpy(condition, "from Users where conn_fd > -1");
	sprintf(query, "select nickname %s", condition);
	printf("query: %s %s\n", query, condition);

	result = query_agent(query, condition);
	return result;
}

sqlr *get_offline_users() {
	char query[256];
	char condition[256];
	sqlr *result;

	strcpy(condition, "from Users where conn_fd = -1");
	sprintf(query, "select nickname %s", condition);
	printf("query: %s %s\n", query, condition);

	result = query_agent(query, condition);
	return result;
}

sqlr *get_online_users_by_roomname(char *roomname) {
	char query[256];
	char condition[256];
	sqlr *result;

	sprintf(condition, "from Authen inner join Users on Authen.username = Users.username where Authen.roomname = '%s' and Users.conn_fd > -1",roomname);
	sprintf(query, "select Users.nickname %s", condition);
	printf("query: %s %s\n", query, condition);

	result = query_agent(query, condition);
	return result;
}

sqlr *get_offline_users_by_roomname(char *roomname) {
	char query[256];
	char condition[256];
	sqlr *result;

	sprintf(condition, "from Authen inner join Users on Authen.username = Users.username where Authen.roomname = '%s' and Users.conn_fd = -1",roomname);
	sprintf(query, "select Users.nickname %s", condition);
	printf("query: %s %s\n", query, condition);

	result = query_agent(query, condition);
	return result;
}

sqlr *get_userfd(sqlr *name) {
	int i;
	char query[1024];
	char condition[1024];
	sqlr *result;

	for (i = 0; i < name->size; i++) {
		if (i == 0)
			strcpy(condition, "from Users where username = '");
		else
			strcat(condition, "or username = '");
		strcat(condition, name->row[i][0]);
		strcat(condition, "' ");
	}
	sprintf(query, "select conn_fd %s", condition);
	printf("query: %s %s\n", query, condition);
 
	result = query_agent(query, condition);
	return result;
}


void *transfer_file(void *arg) {
	request *reqP = (request *)arg;
	printf("in: %s\n", reqP->queryfile);
	int fd, ret;
    char filepath[138], buf[1001], request[1024];

    sprintf(filepath, "files/%s", reqP->queryfile);
    fd = open(filepath, O_RDONLY);

    while((ret = read(fd, buf, 1000)) > 0) {
        buf[ret] = '\0';
        sprintf(request, "%s,%s,%d,%s",DOWNLOAD, DOWNLOAD_S, (int)strlen(buf), buf);
        printf("transfering %s\n", request);
        if(send(reqP->conn_fd, request, strlen(request), 0) < 0)
            ERR_EXIT("Send failed");
    }
    sprintf(request, "%s,%s,", DOWNLOAD, DOWNLOAD_E);
    printf("%s %d\n", request, (int)strlen(request));
    if(send(reqP->conn_fd, request, strlen(request), 0) < 0)
        ERR_EXIT("Send failed");
    close(fd);
    puts("finish transfering");

    return NULL;
}