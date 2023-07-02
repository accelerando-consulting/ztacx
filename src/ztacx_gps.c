#include "ztacx.h"

#include <zephyr/types.h>
#include <errno.h>
#include <zephyr/drivers/uart.h>
#include <math.h>
#include <zephyr/bluetooth/bluetooth.h>

static bool ready = false;
double lat = 0;
double lon = 0;
double firstlat = 0;
double firstlon = 0;
double movement_threshold = 0;
int hour=0,minute=0,second=0,day=0,month=0,year=0,zone=10,zmin=0;

extern void on_movement(double lat, double lon, double from_last, double from_start, double speed);

static char gps_buf[512];
static int  gps_buf_len = 0;


static const struct device *gps_dev;
static char location_buf[32];
static char time_buf[32];

void set_advertised_location(struct bt_data *bt_data, double lat, double lon)
{
	//printk("    updating advertised location to %s\n", location_str());
	memcpy((uint8_t *)bt_data->data+2, (uint8_t*)&lat, sizeof(lat));
	memcpy((uint8_t *)bt_data->data+2+sizeof(lat), (uint8_t*)&lon, sizeof(lon));
}

int fmt_location_str(char *buf, int buf_max, double lat1, double lon1)
{
	int lat_i = lat1;
	int lon_i = lon1;
	double latf = lat1 - lat_i;
	double lonf = lon1 - lon_i;
	if (latf<0) latf=-latf;
	if (lonf<0) lonf=-lonf;
	int latf_i = latf*1e8;
	int lonf_i = lonf*1e8;
	int sz = 0;

	sz += snprintf(buf, buf_max,
		       "[%d.%08d",lat_i,latf_i
		);

	sz += snprintf(buf+sz, buf_max-sz,
		       ",%d.%08d]",lon_i,lonf_i
		);

	return sz;
}

const char *location_str()
{
	fmt_location_str(location_buf, sizeof(location_buf), lat, lon);
	return location_buf;
}

const char *time_str()
{
	snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d", hour, minute,second);
	return time_buf;
}

const double PIx = 3.141592653589793;
const double EARTH_RADIUS = 6371; // Mean radius of Earth in Km

static double convertToRadians(double val) {
   return val * PIx / 180;
}

double distance(double lat1, double lon1, double lat2, double lon2)
{
	double dlon = convertToRadians(lon2 - lon1);
	double dlat = convertToRadians(lat2 - lat1);

	double a = ( pow(sin(dlat / 2), 2) + cos(convertToRadians(lat1))) * cos(convertToRadians(lat2)) * pow(sin(dlon / 2), 2);
	double angle = 2 * asin(sqrt(a));

	return angle * EARTH_RADIUS * 1000;
}

double distance_from(double lat2, double lon2)
{
	return distance(lat, lon, lat2, lon2);
}

static void gps_location_change()
{
	static bool first = true;
	static int64_t last_move = 0;
	static double latwas=0;
	static double lonwas=0;
	int64_t now = k_uptime_get();

	bool changed = false;
	double dist;
	double from_start;
	double duration,speed;

	if ((lat == latwas) && (lon == lonwas)) {
		// no movement
		return;
	}

	dist = distance_from(latwas, lonwas);
	// ignore minor jitter in location of < 10cm
	if (dist < movement_threshold) {
		// movement smaller than jitter threshold, ignore
		return;
	}

	changed = true;
	duration = (now - last_move)/1000;
	if ((last_move==0) || (duration==0)) {
		speed = 0;
	}
	else {
		speed = dist/duration*3.6;
	}
	if (first) {
		firstlat = lat;
		firstlon = lon;
		first = false;
		from_start = 0;
	}
	else {
		from_start = distance_from(firstlat, firstlon);
	}

	latwas = lat;
	lonwas = lon;
	last_move = now;
	on_movement(lat, lon, dist, from_start, speed);
}


static int gps_location_line(char *buf)
{
	//printk("    INFO $GPGLL %s\n", buf);
#if CONFIG_NEWLIB_LIBC
	char *field;
#else
	char *field,*last;
#endif
	char *decimal;
	int fieldpos = 1;
	char *sep = ",";
	char word[16];
	int len,val;
	double mm;

	if (buf[0]==',') {
		//LOG_WRN("GPS: no lock");

		if (strncmp(buf,",,,,",4) == 0) {
			// We have no location but perhaps a time, skip to
			// that part
			fieldpos=5;
			buf+=4;
		}
		else {
			return -1;
		}
	}


	/*
	 * GPGLL
	 *
	 *  Geographic Position, Latitude / Longitude and time.
	 *
	 *  eg1. $GPGLL,3751.65,S,14507.36,E*77
	 *  eg2. $GPGLL,4916.45,N,12311.12,W,225444,A
	 *
	 *
	 *	      4916.46,N    Latitude 49 deg. 16.45 min. North
	 *	      12311.12,W   Longitude 123 deg. 11.12 min. West
	 *	      225444       Fix taken at 22:54:44 UTC
	 *	      A            Data valid
	 *
	 *
	 * eg3. $GPGLL,5133.81,N,00042.25,W*75
	 *		  1    2     3    4 5
	 *
	 *	 1    5133.81   Current latitude
	 *	 2    N         North/South
	 *	 3    00042.25  Current longitude
	 *	 4    W         East/West
	 *	 5    *75       checksum
	 *   $--GLL,lll.ll,a,yyyyy.yy,a,hhmmss.ss,A llll.ll = Latitude of position
	 *
	 *   a = N or S
	 *   yyyyy.yy = Longitude of position
	 *   a = E or W
	 *   hhmmss.ss = UTC of position
	 *   A = status: A = valid data
	 */
#if CONFIG_NEWLIB_LIBC
	for (field=strtok(buf, sep);
	     field;
	     (field=strtok(NULL, sep)),fieldpos++) {
#else
	for (field=strtok_r(buf, sep,&last);
	     field;
	     (field=strtok_r(NULL, sep,&last)),fieldpos++) {
#endif
		switch (fieldpos) {
		case 1:
			// first word of LL sentence is DDMM.SSSSSSS
			//
			// Get DD the degrees
			decimal = strchr(field, '.');
			if (!decimal) {
				//LOG_WRN("No decimal point in latitude");
				return -1;
			}
			// part before decimal is either DDDMM or DDMM
			len = decimal-field-2;
			strncpy(word, field,len);
			word[len]='\0';
			val = atoi(word);
			//printk("    field=[%s] word=[%s] len=%d deg_int=%d\n", field, word, len, val);
			lat = val;
			//printk("    lat=>%lf\n", lat);

			//
			// Get MM the minutes whole part (1/60 deg)
			//
			if (sscanf(decimal-2,"%lf",&mm)) {
				//printk("    Oooh scanf works, got %lf\n", mm);
				lat += mm/60;
			}
			else {
				strncpy(word, decimal-2,2);
				word[2]='\0';
				val = atoi(word);
				mm = val/60;
				lat += mm;
				//printk("    word=[%s] min_int=%d\n", word, val);
				//printk("    mm=%lf lat=>%lf\n", mm, lat);

				// fractional part
				val = atoi(decimal+1);
				//printk("    word=[%s] min_frac=%d\n", decimal, val);
				//printk("    mm=%lf lat=>%lf\n", mm, lat);
				mm = val/6000;
				lat += mm;
			}
			break;
		case 2:
			// second word is latitude sign
			if (field[0]=='S') lat = -lat;
			break;
		case 3:
			// third word is longitude
			//
			// Get DD the degrees
			decimal = strchr(field, '.');
			if (!decimal) {
				//LOG_WRN("No decimal point in latitude");
				return -1;
			}
			// part before decimal is either DDDMM or DDMM
			len = decimal-field-2;
			strncpy(word, field,len);
			word[len]='\0';
			lon = atoi(word);
			if (sscanf(decimal-2,"%lf",&mm)) {
				//printk("    Oooh scanf works, got %lf\n", mm);
				lon += mm/60;
			}
			else {
				//FIXME: this code is busted.

				//
				// Get MM the minutes whole part (1/60 deg)
				strncpy(word, decimal-2,2);
				word[2]='\0';
				val = atoi(word);
				mm = val;

				// fractional part
				val = atoi(decimal+1);
				mm += val/100;
				lon += mm/60;
			}


			break;
		case 4:
			// fourth word is longitude sign
			if (field[0]=='W') lon = -lon;
			//printk("    INFO Parsed location %s\n", location_str());
			break;
		case 5:
			if (strlen(field) < 6) {
				// Time cannot be in form HHMMSS.  Give up!
				//LOG_INF("No time source in GPGLL sentence");
				return -1;
			}
			// fifth word is time (GMT)
			strncpy(word, field, 2);
			word[2]='\0';
			hour = (atoi(word)+zone)%24;

			strncpy(word, field+2, 2);
			word[2]='\0';
			minute = (atoi(word)+zmin)%60;

			strncpy(word, field+4, 2);
			word[2]='\0';
			second = atoi(word);
			snprintf(word,sizeof(word), "%02d:%02d:%02d", hour, minute, second);
			//LOG_INF("Parsed GPS Timestamp %s (local time)", word);
			break;
		}
	}

	gps_location_change();

	return 0;
}

static int gps_date_line(char *buf)
{
	printk("    gps_date_line %s\n", buf);
#if CONFIG_NEWLIB_LIBC
	char *field;
#else
	char *field,*last;
#endif
	int fieldpos;
	char *sep = ",";
	char word[30];

	/*
	 * Date & Time
	 *
	 * UTC, day, month, year, and local time zone.
	 *
	 * $--ZDA,hhmmss.ss,xx,xx,xxxx,xx,xx
	 * hhmmss.ss = UTC
	 * xx = Day, 01 to 31
	 * xx = Month, 01 to 12
	 * xxxx = Year
	 * xx = Local zone description, 00 to +/- 13 hours
	 * xx = Local zone minutes description (same sign as hours)
	 *
	 */

#if CONFIG_NEWLIB_LIBC
	for (field=strtok(buf, sep),fieldpos=1;
	     field;
	     (field=strtok(NULL, sep)),fieldpos++) {
#else
	for (field=strtok_r(buf, sep,&last),fieldpos=1;
	     field;
	     (field=strtok_r(NULL, sep,&last)),fieldpos++) {
#endif
		switch (fieldpos) {
		case 1:
			// first word is time (GMT)
			strncpy(word, field, 2);
			word[2]='\0';
			hour = (atoi(word)+10)%24;

			strncpy(word, field+2, 2);
			word[2]='\0';
			minute = atoi(word);

			strncpy(word, field+4, 2);
			word[2]='\0';
			second = atoi(word);
			break;
		case 2:
			// second word is day of month 01 to 31
			day = atoi(field);
			break;
		case 3:
			// third word is month 01 to 12
			month = atoi(field);
			break;
		case 4:
			// fourth word is year
			year = atoi(field);
			break;
		case 5:
			// fifth word is timezone hours
			zone = atoi(field);
			break;
		case 6:
			// sixth word is timezone minutes
			zmin = atoi(field);
			snprintf(word, sizeof(word), "%d-%02d-%02dT%02d:%02d:%02dZ%02d%02d",
				 year, month, day, hour, minute, second, zone, zmin);
			//LOG_INF("Parsed date/time [%s]", word);
			break;
		}
	}

	return 0;
}


static int gps_line(char *buf)
{
	//LOG_INF("%s", buf);
	char *comma = strchr(buf, ',');
	char *sentence;

	if (!comma) {
		//LOG_WRN("Unrecognised GPS input [%s]", buf);
		return -1;
	}

	sentence = comma+1;
	*comma = '\0';

	if (strcmp(buf, "$GPGLL")==0) {
		// location
		return gps_location_line(sentence);
	}
	else if (strcmp(buf, "$GPGGA")==0) {
		// $GPGGA - Global Positioning System Fix Data
		//LOG_INF("%s,%s", buf,sentence);
	}
	else if (strcmp(buf, "$GPRMC")==0) {
		// $GPRMC - Recommended minimum specific GPS/Transit data
	}
	else if (strcmp(buf, "$GPTXT")==0) {
		// $GPTXT - boot banner from device
		printk("    %s,%s\n", buf,sentence);
	}
	else if (strcmp(buf, "$GPVTG")==0) {
		// $GPVTG - Track made good and ground speed
	}
	else if (strcmp(buf, "$GPGSA")==0) {
		// $GPGSA - GPS DOP and active satellites
	}
	else if (strcmp(buf, "$GPGSV")==0) {
		// $GPGSV - GPS Satellites in view
	}
	else if (strcmp(buf, "$GPGZDA")==0) {
		return gps_date_line(sentence);
	}
	else {
		// something we didn't handle above
		printk("     GPS unhandled %s,%s\n", buf,sentence);
	}
	return 0;
}


static void gps_rx_isr(void)
{
	int rx;
	int count = 0;

	if (gps_buf_len >= (sizeof(gps_buf)-5)) {
		gps_buf_len = 0;
		gps_buf[0] = '\0';
	}

	do {
		rx = uart_fifo_read(
			gps_dev,
			gps_buf+gps_buf_len,
			sizeof(gps_buf)-gps_buf_len-1);
		count+=rx;
		gps_buf_len += rx;
		gps_buf[gps_buf_len] = '\0';
	} while (rx);
}


static void gps_isr(const struct device *unused, void *user_data)
{
	ARG_UNUSED(unused);
	ARG_UNUSED(user_data);

	//LOG_INF("gps_isr");
	if (uart_irq_rx_ready(gps_dev)) {
		gps_rx_isr();
	}
}

void gps_setup()
{
	int err;

	gps_dev = device_get_binding("UART_1");
	if (gps_dev == NULL) {
		printk("WARNING: GPS uart not configured\n");
	}
	else {
		printk("GPS on UART_1 (interrupt)\n");

		uart_irq_rx_disable(gps_dev);
		uart_irq_tx_disable(gps_dev);

		//LOG_INF("Enabling RX ISR");
		uart_irq_callback_set(gps_dev, gps_isr);
		uart_irq_rx_enable(gps_dev);
	}
	ready = true;
}

int gps_loop()
{
	if (!ready || !gps_dev) {
		return -1;
	}
	int lines = 0;

	char *nlpos;

	if ((nlpos = strchr(gps_buf, '\n')) != NULL) {
		char gps_line_buf[100]="";
		*(nlpos++)='\0';
		strncpy(gps_line_buf, gps_buf, sizeof(gps_line_buf));
		int remain = strlen(nlpos);
		memmove(gps_buf, nlpos, remain);
		gps_buf_len = remain;

		gps_line(gps_line_buf);
		++lines;
	}
	return lines;
}
