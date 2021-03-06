#include	"mg301.h"

void mg_init(void) {
uint8_t i;
	
	MG301_HALT();
	sleep(1);
	MG301_PWON();
	sleep(5);
	disable_echo();
	for(i=0; i<60; i++) {
		if(is_rcv_nwtime()) {
			debug("update time from NWTIME\r\n");
			update_time();
			usart2_buf_clr();
			break;
		}
		sleep(1);
	}
}

void mg_cmd(const char *str) {
	usart2_buf_clr();
	yputs(str);
	yputs("\r\n");
	delay(CMD_WAIT_TIME);
}

uint8_t is_gm301_on(void) {
	mg_cmd("AT");
	sleep(1);
	mg_cmd("AT");
	if(strstr(get_usart2_buf(), "OK")) {
		return 1;
	}
	
	return 0;
}

void disable_echo(void) {
	mg_cmd("ATE0");
	delay(CMD_WAIT_TIME);
}

uint8_t is_rcv_nwtime(void) {
	if(strstr(get_usart2_buf(), "NWTIME") && strstr(get_usart2_buf(), "+")) {
		return 1;
	}
	return 0;
}

void update_time(void) {
char *p, time[16], date[16];
uint8_t hour;
	
	p = strstr(get_usart2_buf(), "NWTIME");
	if(p != NULL) {
		strncpy(date, p+sizeof("NWTIME: "), 8);
		strncpy(time, p+sizeof("NWTIME: ")+9, 8);
		hour = (time[0]-'0')*10 + (time[1]-'0');
		hour += 8;
		time[0] = ((hour%24)/10)+'0';
		time[1] = ((hour%24)%10)+'0';
		time[8] = 0;
		date[8] = 0;
		set_date(date);
		
		if(hour >= 24) {
			//更新跨天日期时禁止RTC中断，避免重复进入RTC中断的问题
			NVIC_DisableIRQ(RTC_IRQn);;
			set_time("23:59:59");
			sleep(2);
			RTC->ISR &= ~RTC_ISR_ALRAF;
			EXTI->PR |= EXTI_PR_PIF17;
			NVIC_EnableIRQ(RTC_IRQn);;
		}
		set_time(time);
	}
}

uint8_t set_profile(uint8_t id, const char *server, const char *apn, const char *user, const char *passwd) {
char str[128];
	sprintf(str, "%s=%d,conType,%s\n", "AT^SICS", id, "GPRS0");
	mg_cmd(str);
	
	sprintf(str, "%s=%d,apn,%s\n", "AT^SICS", id, apn);
	mg_cmd(str);

	sprintf(str, "%s=%d,user,%s\n", "AT^SICS", id, user);
	mg_cmd(str);

	sprintf(str, "%s=%d,passwd,%s\n", "AT^SICS", id, passwd);
	mg_cmd(str);

	sprintf(str, "%s=%d,srvType,%s\n", "AT^SISS", id, "Socket");
	mg_cmd(str);

	sprintf(str, "%s=%d,conId,%d\n", "AT^SISS", id, id);
	mg_cmd(str);
	
	sprintf(str, "%s=%d,address,\"socktcp://%s\"\n", "AT^SISS", id, server);
	mg_cmd(str);
	
	return 0;
}

uint8_t net_open(uint8_t id) {
char str[32], msg[32];
uint8_t i, retry, ret=1;
char header[] = "460029125715486";	
	
	sprintf(str, "%s=%d\n", "AT^SISO", id);
	
	for(retry=0; retry<3; retry++) {
		mg_cmd("AT+CHUP");
		sleep(1);
		mg_cmd(str);
		for(i=0; i<5; i++) {
			sleep(1);
			if(is_net_connected(0)) {
				if(net_puts(0, header) == 0) {
					sleep(5);
					if(net_read(0, msg, 32) != 0) {
						if(msg[0] == '#') {
						char date[10], time[10];
							date[0] = msg[1]; date[1] = msg[2]; date[2] = '/';
							date[3] = msg[3]; date[4] = msg[4]; date[5] = '/';
							date[6] = msg[5]; date[7] = msg[6]; date[8] = 0;
							
							time[0] = msg[7]; time[1] = msg[8]; time[2] = ':';
							time[3] = msg[9]; time[4] = msg[10]; time[5] = ':';
							time[6] = msg[11]; time[7] = msg[12]; time[8] = 0;

							debug("update time from server.\r\n");
							set_date(date);
							set_time(time);
							return 0;
						}					
					}
				}
			}		
		}
	}
	return ret;
}

uint8_t is_net_connected(uint8_t id) {
char findstr[32];
char *p;
uint8_t i, connected=0;	
	
	sprintf(findstr, "%s %d", "^SISO:", id);
	mg_cmd("AT^SISO?");
	if((p=strstr(get_usart2_buf(), findstr)) != NULL) {
		for(i=0; i<128; i++) {
			if(p[i] == 0x0a){
				p[i] = 0x00;
				break;
			}
		}
		sprintf(findstr, "%s", "\"4\",\"2\"");
		if(strstr(p, findstr)) {
			connected=1;
		}
	}		
	
	return connected;
}

uint8_t net_write(uint8_t id, const char *buf, uint16_t len) {
char sisw[128] = "AT^SISW=0,";
uint8_t retry, ret=1;
uint16_t i;
	
	sprintf(sisw, "%s=%d,%d\n", "AT^SISW", id, len);
	for(retry=0; retry<3; retry++) {
		mg_cmd("AT+CHUP");
		sleep(1);
		mg_cmd(sisw);
		for(i=0; i<len; i++) {
			yputc(buf[i]);
		}
		for(i=0; i<5; i++) {
			sleep(1);
			if(strstr(get_usart2_buf(), "OK")) {
				return 0;
			} else if(strstr(get_usart2_buf(), "ERROR")) {
				return 1;
			}	
		}
	}
	
	return ret;
}

uint8_t net_puts(uint8_t id, const char *msg) {
char sisw[128] = "AT^SISW=0,";
int len;
uint8_t i, retry, ret=1;
	
	len = strlen(msg);
	sprintf(sisw, "%s=%d,%d\n", "AT^SISW", id, len);
	mg_cmd("AT+CHUP");
	sleep(1);
	mg_cmd(sisw);
	mg_cmd(msg);	
	for(retry=0; retry<3; retry++) {
		for(i=0; i<5; i++) {
			sleep(1);
			if(strstr(get_usart2_buf(), "OK")) {
				return 0;
			} else if(strstr(get_usart2_buf(), "ERROR")) {
				return 1;
			}
		}
	}
	
	return ret;
}

uint8_t net_read(uint8_t id, char *buf, uint16_t len) {
uint16_t num=0, i;
char read[32]= "AT^SISR=0,", temp[32];
char *p;	
	
	for(i=0; i<10; i++) {
		mg_cmd("AT+CHUP");
		sleep(1);
		mg_cmd("AT^SISR=0,0");
		if((p=strstr(get_usart2_buf(), "SISR: 0,")) != NULL) {
			p += strlen("SISR: 0,");
			num = atoi(p);
			if(num == 0) {
				continue;
			}
			if((num > 0) && (num <= len)) {
				sprintf(temp, "%d\n", num);
				strcat(read, temp);
				mg_cmd(read);
				if((p = strstr(get_usart2_buf(), "SR:")) != NULL) {
					while(*p++ != '\n');
					while(num--) {
						*buf++ = *p++;
					}
					*buf = 0;
				}
			}
			break;
		}
	}
	return num;
}

uint8_t net_close(uint8_t id) {
char sisc[32];
uint8_t i, retry, ret=1;
	
	sprintf(sisc, "%s=%d\n", "AT^SISC", id);
	for(retry=0; retry<3; retry++) {
		mg_cmd(sisc);
		for(i=0; i<5; i++) {
			sleep(1);
			if(strstr(get_usart2_buf(), "OK")) {
				return 0;
			} else if(strstr(get_usart2_buf(), "ERROR")) {
				return 1;
			}
		}
	}
	
	return ret;
}

uint8_t send_sms(char *phone, char *msg) {
char cmgs[32] = "AT+CMGS=";
	
	mg_cmd("AT+CMGF=1");
	mg_cmd("AT+CPMS=\"SM\",\"SM\",\"SM\"");
	mg_cmd("AT+CSCA=+8613800871500");
	strcat(cmgs, (char *)phone);
	mg_cmd(cmgs);
	while(!(strstr(get_usart2_buf(), ">")));
	mg_cmd(msg);
	mg_cmd("\x1A");
	
	return 0;
}

uint8_t is_ring(const char *phone) {
uint8_t ret=0;
	
	if(strstr(get_usart2_buf(), "RING")) {
		mg_cmd("AT+CLCC");
		if(strstr(get_usart2_buf(), phone) == NULL) {
			sleep(5);
		} else {
			ret = 1;
		}
		mg_cmd("AT+CHUP");
		sleep(1);		
	}
	
	return ret;
}

uint8_t get_rssi(void) {
char csq[32];
char i, j;
uint8_t rssi;	
	
	mg_cmd("AT+CSQ");
	if(strstr(get_usart2_buf(), "OK")) {
		memcpy(csq, get_usart2_buf(), 32);
		for(i=0; i<32; i++) {
			if(csq[i] == ':') {
				j=i;
			} else if(csq[i] == ',') {
				csq[i] = 0;
				rssi = (uint8_t)atoi(csq+j+1);
				break;
			}
		}
	}
	
	return rssi;
}
