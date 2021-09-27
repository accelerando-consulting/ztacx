#ifndef _APP_GPS_H
#define _APP_GPS_H 1

extern double lat;
extern double lon;

extern int fmt_location_str(char *buf, int buf_max, double lat1, double lon1);
extern const char *location_str();
extern const char *time_str();
extern double distance(double lat1, double lon1, double lat2, double lon2);
extern double distance_from(double lat2, double lon2);
extern void set_advertised_location(struct bt_data *bt_data, double lat, double lon);

extern void gps_setup();
extern int gps_loop();


#endif
