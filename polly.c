/*
**
** (C) 2012 Adrian Ulrich
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
**
**
*/

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "polly.h"



int main() {
	int afd, pfd, cfd;
	char rbuff[GBUFFSIZE];
	size_t bytes_read;
	int atlen  = strlen(AT_PREFIX);
	
	/* make socket unreadable */
	umask( 0777 );
	
	/* get unix domain socket and pts */
	afd = get_audio_socket();
	pfd = get_pts_socket();
	
	/* only allow write access to the media user */
	chown(SOCKET_PATH, USER_MEDIA, GROUP_AUDIO);
	chmod(SOCKET_PATH, S_IWUSR | S_IRUSR );
	
	/* drop root */
	setgid(GROUP_AUDIO);
	setuid(USER_MEDIA);
	if(setuid(0) != -1)
		xdie("failed to drop root!");
	
	
	DMSG("socket setup finished. afd=%d, pfd=%d", afd, pfd);
	
	while( (cfd = accept(afd, NULL, NULL)) ) {
		
		/* read incomding data: we expect something like this:
		   path,40,5,3,0,0,1,3,0,1,0,1,2,13
		  ..but we need..
		  AT+XDRV=40,5,3,0,0,1,3,0,1,0,1,2,13
		  the AT command string is 3 bytes longer, so we read to rbuff[3]
		  and overwrite the rest afterwards
		*/
		memset(&rbuff, 0, GBUFFSIZE);
		bytes_read = read(cfd, &rbuff[3], GBUFFSIZE-3-2); /* -3 = offset, -2 = \r\0 */
		memcpy(&rbuff, AT_PREFIX, atlen);                 /* add AT+XDRV=           */
		memcpy(&rbuff[bytes_read+3], "\r\0", 2);          /* terminate string       */
		
		/* send command to modem if it looks ok */
		if(bytes_read >= CALLVOLUME_CMDLEN &&
		       at_args_sane(&rbuff[atlen], bytes_read+3-atlen) == 1) {
			send_xdrv_command(rbuff, pfd);
		}
		else {
			DMSG("silently dropping invalid command with %d bytes len", bytes_read);
		}
		
		close(cfd);
	}
	
	DMSG("exiting, accept returned false on fd %d", afd);
	
	close(afd);
	close(pfd);
	
	xdie("terminating");
	return 0;
}


/*
** Check if given string only contains 0-9 and ,
*/
int at_args_sane(char *buffer, size_t bufflen) {
	size_t i;
	int sane=1;
	for(i=0; i<bufflen;i++) {
		if(buffer[i] >= '0' && buffer[i] <= '9')
			continue;
		if(buffer[i] == ',')
			continue;
		sane = 0;
		break;
	}
	return sane;
}


/*
** Creates the socket used by libcallvolume
*/
int get_audio_socket() {
	struct sockaddr_un saddr;
	int afd;
	
	if( (afd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0 )
		xdie("Could not create new socket");
	
	saddr.sun_family = AF_UNIX;
	strcpy(saddr.sun_path, SOCKET_PATH);
	
	unlink(SOCKET_PATH); // may fail
	
	if( bind(afd, (struct sockaddr*)&saddr, sizeof(struct sockaddr_un) ) != 0 )
		xdie("bind failed");
	
	if(listen(afd, 64))
		xdie("listen failed!");
	
	return afd;
}




/*
** Send an AT command to the modem
** Returns != 0 if the reply contained OK
*/
void send_xdrv_command(const char *cmd, int fd) {
	char atbuff[GBUFFSIZE];
	size_t bread;
	struct timespec tx;
	
	/* the modem needs some time -fixme: this is hacky */
	tx.tv_sec = 0;
	tx.tv_nsec = 75000000;
	
	DMSG(">>> %s", cmd);
	
	write(fd, cmd, strlen(cmd));
	
	nanosleep(&tx, NULL);
	
	bread = read(fd, &atbuff, GBUFFSIZE);
	
	DMSG("<<< %d bytes", bread);
	/* fixme: this should probably search for \r\nOK\r\n */
}




/*
** Try to grab a new pty
** Returns an opened FD, -1 on error
*/
int get_pts_socket() {
	int i;
	int pts_fd = -1;
	char pts_path[GBUFFSIZE];
	char fuser_cmd[GBUFFSIZE];
	
	for(i=MUX_PTS_LAST;i>=MUX_PTS_FIRST;i--) {
		snprintf(pts_path, GBUFFSIZE,"/dev/pts/%d", i);
		snprintf(fuser_cmd, GBUFFSIZE, "/system/xbin/fuser %s", pts_path);
		
		DMSG("Searching for %s, executing %s", pts_path, fuser_cmd);
		
		if( (system(fuser_cmd) != 0) && (pts_fd = open(pts_path, O_RDWR)) != -1 ) {
			DMSG("Free pty is at %s, opened as fd %d", pts_path, pts_fd);
			break;
		}
	}
	
	return pts_fd;
}




/*
** Die with a fatal error
*/
void xdie(char *msg) {
	fprintf(stderr, "FATAL: %s - exiting in %d seconds\n", msg, SEC_SLEEP);
	sleep(SEC_SLEEP); /* we get respawn by init - sleep */
	exit(1);
}
