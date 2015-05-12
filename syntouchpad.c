/*
 * Copyright (C) 2013 Synaptics Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <errno.h>
#include <string.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/select.h>
#include <time.h>
#include <linux/limits.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/filter.h>
#include <sys/inotify.h>


/* Only store the first 32 keyboard and touchpad devices! */
#define SYN_MAX_KEYBOARDS	32
#define SYN_MAX_TOUCHPADS	32

#define SYN_TOUCHPAD_MANAGER_DIR "/data/system"
#define SYN_TOUCHPAD_MANAGER_FILE SYN_TOUCHPAD_MANAGER_DIR "/syntouchpad"

 #define SYNAPTICS_VENDOR	0x06CB

int palm_check_keypress_timeouts[] = { 0, 200, 400, 650, 750, 850, 1000, 1000 };
int palm_check_highw[] = { 15, 14, 13, 12, 10, 9, 8, 7 };


struct touchpad_data
{
	int suppress_fd;
	int highw_fd;
	int suppress_click_fd;
	struct timespec suppress_time;
	int suppressed;
	long long timeout;
};

const char cmd_suppress_on = '1';
const char cmd_suppress_off = '0';

int contains_nonzero_byte(const uint8_t * bitmask, int start_index, int end_index)
{
	const uint8_t * end = bitmask + end_index;
	bitmask += start_index;
	if (bitmask != end) {
		if (*bitmask != 0) {
			return 1;
		}
		++bitmask;
	}

	return 0;
}

void find_keyboards(unsigned int * keyboards, int * index)
{
	struct dirent * input_dir_entry;
	DIR * event_dir;
	int fd;
	uint8_t keyBitmask[(KEY_MAX + 1) / 8];
	char pathname[PATH_MAX];
	int i;

	if (*index > 0) {
		/* clear the old values */
		for(i = 0; i > *index; i++) {
			close(keyboards[i]);
			keyboards[i] = 0;
		}
		*index = 0;

	}

	memset(keyBitmask, 0, sizeof(keyBitmask));
	
	event_dir = opendir("/dev/input");
	if (!event_dir)
		return;

	while ((input_dir_entry = readdir(event_dir)) != NULL) {
		if (strstr(input_dir_entry->d_name, "event")) {
			snprintf(pathname, PATH_MAX, "/dev/input/%s", input_dir_entry->d_name);
			fd = open(pathname, O_RDONLY);
			if (fd < 0) {
				continue;
			}
			
			ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBitmask)), keyBitmask);
			if (contains_nonzero_byte(keyBitmask, 0, (BTN_MISC + 7) / 8)
				|| contains_nonzero_byte(keyBitmask, (KEY_OK + 7) / 8,
								(KEY_MAX + 1 + 7) / 8)) 
			{
				/* Is a keyboard */
				keyboards[*index] = fd;
				++*index;
			} else {
				close(fd);
			}
		}
	}
	closedir(event_dir);
}

int read_touchpad_data_file(int * palm_check_setting, int * enable_click, int * enable_touchpad, int * disable_for_ext_mouse)
{
	int len;
	int fd;
	char file_data[1024];

	/* set defaults in case the open or read fails */
	*enable_click = 1;
	*palm_check_setting = 3;
	*enable_touchpad = 1;
	*disable_for_ext_mouse = 0;

	fd = open(SYN_TOUCHPAD_MANAGER_FILE, O_RDONLY);
	if (fd < 0) {
	 	perror("open data file");
		return 1;
	}

	len = read(fd, file_data, 1024);
	if (len < 0) {
		perror("read data file");
		close(fd);
		return 1;
	}

	sscanf(file_data, "%d\n%d\n%d\n%d", palm_check_setting, enable_click, enable_touchpad, disable_for_ext_mouse);

	close(fd);

	return 0;
}

void update_palm_check_setting(struct touchpad_data * tpd, int palm_check_setting)
{
	char buff[256];
	int count;

	if (palm_check_setting < 0 && palm_check_setting > 7)
		return; 

	tpd->timeout = palm_check_keypress_timeouts[palm_check_setting] * 1000;
	count = snprintf(buff, 256, "%d", palm_check_highw[palm_check_setting]);
	write(tpd->highw_fd, buff, count);

#ifdef DEBUG
	fprintf(stdout, "update_palm_check_setting: timeout = %lld highw = %s\n",
		tpd->timeout, buff);
#endif
}

void update_enable_click_setting(struct touchpad_data * tpd, int enable_click)
{
	int rc;

	if (enable_click) {
		rc = write(tpd->suppress_click_fd, &cmd_suppress_off, 1);
		if (rc < 0) {
			fprintf(stderr, "update_enable_click_setting: enable write failed %s\n",
				strerror(errno));
		}
	} else {
		rc = write(tpd->suppress_click_fd, &cmd_suppress_on, 1);
		if (rc < 0) {
			fprintf(stderr, "update_enable_click_setting: disable write failed %s\n",
				strerror(errno));
		}
	}

#ifdef DEBUG
	fprintf(stdout, "update_enable_click_setting: enable click = %d\n", enable_click);
#endif
}

void update_enable_touchpad_setting(struct touchpad_data * tpd, int enable_touchpad)
{
	int rc;

	if (enable_touchpad) {
		rc = write(tpd->suppress_fd, &cmd_suppress_off, 1);
		if (rc < 0) {
			fprintf(stderr, "update_enable_touchpad_setting: enable write failed %s\n",
				strerror(errno));
		}
	} else {
		rc = write(tpd->suppress_fd, &cmd_suppress_on, 1);
		if (rc < 0) {
			fprintf(stderr, "update_enable_touchpad_setting: disable write failed %s\n",
				strerror(errno));
		}
	}

#ifdef DEBUG
	fprintf(stdout, "update_enable_touchpad_setting: enable touchpad = %d\n", enable_touchpad);
#endif
}


int open_device_control_file(int * fd, const char * pathname)
{
	struct timespec ts;
	int i;
	int rc = 0;

#ifdef DEBUG
	fprintf(stdout, "touchpad: %s\n", pathname);
#endif

	for (i = 0; i < 10; ++i) {
		*fd = open(pathname, O_WRONLY);
		if (*fd < 0) { 
			fprintf(stderr, "open %s failed: %s\n", pathname, strerror(errno));
			if (errno == ENOENT) {
				ts.tv_sec = 0;
				ts.tv_nsec = 10 * 1000 * 1000;
				nanosleep(&ts, NULL);
				continue;
			} else {
				fprintf(stderr, "open %s failed: %s\n", pathname, strerror(errno));
				return -1;
			}
		}
		break;
	}

	return rc;
}

int find_external_mice()
{
	struct dirent * input_dir_entry;
	DIR * input_dir;

	input_dir = opendir("/sys/class/input");
	if (!input_dir)
		return 0;

	while ((input_dir_entry = readdir(input_dir)) != NULL) {
		if (strstr(input_dir_entry->d_name, "mouse")) {
			char buf[PATH_MAX];
			char devPath[PATH_MAX];
			int bus;
			int vendor;
			int product;
			int id;

			snprintf(devPath, PATH_MAX, "/sys/class/input/%s/device/device", input_dir_entry->d_name);

			readlink(devPath, buf, PATH_MAX);
			sscanf(buf, "../../../%x:%x:%x:%x", &bus, &vendor, &product, &id);

			if (vendor != SYNAPTICS_VENDOR)
				return 1;
		}
	}

	return 0;
}

void find_touchpads(struct touchpad_data * touchpads, int * index)
{
	struct dirent * devices_dir_entry;
	struct dirent * sensor_dir_entry;
	DIR * devices_dir;
	DIR * sensor_dir;
	char pathname[PATH_MAX];
	int palm_check_setting;
	int enable_click;
	int enable_touchpad;
	int disable_for_ext_mouse;
	const char * fn12_str = "fn12";
	const char * fn11_str = "fn11";
	const char * function_name = fn11_str;
	int fn30 = 0;
	int fn12 = 0;

	if (index > 0) {
		int i;
		for(i = 0; i > *index; i++) {
			close(touchpads[i].suppress_fd);
			close(touchpads[i].highw_fd);
			close(touchpads[i].suppress_click_fd);
			memset(&touchpads[i], 0, sizeof(struct touchpad_data));
		}
		/* clear the old values */
		*index = 0;
	}

	devices_dir = opendir("/sys/devices");
	if (!devices_dir)
		return;

	read_touchpad_data_file(&palm_check_setting, &enable_click, &enable_touchpad, &disable_for_ext_mouse);

	if (find_external_mice() && disable_for_ext_mouse)
		enable_touchpad = 0;

	while ((devices_dir_entry = readdir(devices_dir)) != NULL) {
		if (strstr(devices_dir_entry->d_name, "sensor")) {
			snprintf(pathname, PATH_MAX, "/sys/devices/%s", devices_dir_entry->d_name);
			sensor_dir = opendir(pathname);
			if (!sensor_dir)
				continue;

			while ((sensor_dir_entry = readdir(sensor_dir)) != NULL) {
				if (strstr(sensor_dir_entry->d_name, "fn30"))
					fn30 = 1;
				if (strstr(sensor_dir_entry->d_name, "fn12"))
					fn12 = 1;
			}
			closedir(sensor_dir);

			if (fn30) {
				/* found a sensor with F$30 so assume its a clickpad */
				if (fn12)
					function_name = fn12_str;

				snprintf(pathname, PATH_MAX, 
					"/sys/devices/%s/%s.%s/suppress",
					devices_dir_entry->d_name,
					devices_dir_entry->d_name,
					function_name);
				open_device_control_file(&touchpads[*index].suppress_fd,
					pathname);
				snprintf(pathname, PATH_MAX,
					"/sys/devices/%s/%s.%s/suppress_highw",
					devices_dir_entry->d_name,
					devices_dir_entry->d_name,
					function_name);
				open_device_control_file(&touchpads[*index].highw_fd,
					pathname);
				snprintf(pathname, PATH_MAX,
					"/sys/devices/%s/%s.fn30/suppress",
					devices_dir_entry->d_name,
					devices_dir_entry->d_name);
				open_device_control_file(
					&touchpads[*index].suppress_click_fd,
					pathname);

				update_enable_click_setting(&touchpads[*index],
								 enable_click);
				update_palm_check_setting(&touchpads[*index],
								palm_check_setting);
				update_enable_touchpad_setting(&touchpads[*index],
								 enable_touchpad);
				if (touchpads[*index].suppress_fd >= 0)
					++*index;
			}
		}
	}
	closedir(devices_dir);

}

long long diff_time(struct timespec * start)
{
	long long diff;
	struct timespec curr_time;
	clock_gettime(CLOCK_MONOTONIC, &curr_time);
	diff = (curr_time.tv_sec - start->tv_sec) * 1000 * 1000;
	diff += (curr_time.tv_nsec - start->tv_nsec) / 1000;
	return diff;
}

int main(int argc, char **argv)
{
	unsigned int keyboards[SYN_MAX_KEYBOARDS];
	struct touchpad_data touchpads[SYN_MAX_TOUCHPADS];
	int keyboards_index = 0;
	int touchpads_index = 0;
	int res;
	int i;
	unsigned int max_fd = 0;
	ssize_t data_read;
	int should_suppress;
	struct timeval select_timeout = { .tv_sec = 0, .tv_usec = 100000 };
	struct timeval * pselect_timeout = NULL;
	int suppress_count;
	int touchpad_notify_fd;
	int event_notify_fd;
	int wd;
	int len;
	struct timespec ts;

	if (argc > 1 && strstr(argv[1], "--fake-it")) {
		touchpads[0].suppress_fd = open("/dev/null", O_WRONLY);
		++touchpads_index;
	} else {
		find_touchpads(touchpads, &touchpads_index);
	}

	find_keyboards(keyboards, &keyboards_index);

	event_notify_fd = inotify_init();
	if (event_notify_fd < 0) {
		perror("inotify_init");
	}

	wd = inotify_add_watch(event_notify_fd, "/dev/input", IN_MODIFY | IN_CREATE | IN_DELETE);
	if (wd < 0) {
		perror("inotify_add_watch");
	}

	touchpad_notify_fd = inotify_init();
	if (touchpad_notify_fd < 0) {
		perror("inotify_init");
	}

	for (;;) {
		wd = inotify_add_watch(touchpad_notify_fd, SYN_TOUCHPAD_MANAGER_DIR,
				IN_MODIFY | IN_CREATE | IN_DELETE);
		if (wd < 0) {
			perror("inotify_add_watch");
		} else {
			break;
		}
		ts.tv_sec = 0;
		ts.tv_nsec = 10 * 1000 * 1000;
		nanosleep(&ts, NULL);
	}

	for (;;) {
		should_suppress = 0;
		fd_set fds;
		FD_ZERO(&fds);
		for (i = 0; i < keyboards_index; ++i) {
			if (max_fd < keyboards[i])
				max_fd = keyboards[i];
			FD_SET(keyboards[i], &fds);
		}
		FD_SET(event_notify_fd, &fds);
		if (max_fd < event_notify_fd)
		 	max_fd = event_notify_fd;
		FD_SET(touchpad_notify_fd, &fds);
		if (max_fd < touchpad_notify_fd)
		 	max_fd = touchpad_notify_fd;

		res = select(max_fd + 1, &fds, NULL, NULL, pselect_timeout);
		if (res < 0) {
			if (res == EINTR) {
				continue;
			} else {
				perror("select()");
				return 2;
			}
		}

		if (res > 0) {
			if (FD_ISSET(event_notify_fd, &fds)) {
				struct inotify_event * event;
				const int buff_len = sizeof(struct inotify_event) + NAME_MAX + 1;
				char buff[buff_len];

				len = read(event_notify_fd, buff, buff_len);
				if (len < 0) {
					perror("read notify fd");
					continue;
				}

				event = (struct inotify_event *)buff;

				fprintf(stdout, "inotify_event: name (%s) mask (0x%x)\n",
					event->name, event->mask);

				if (!strstr(event->name, "event"))
					continue;

				find_touchpads(touchpads, &touchpads_index);
				find_keyboards(keyboards, &keyboards_index);
				continue;
			}

			if (FD_ISSET(touchpad_notify_fd, &fds)) {
				struct inotify_event * event;
				const int buff_len = sizeof(struct inotify_event) + NAME_MAX + 1;
				char buff[buff_len];
				int enable_click;
				int enable_touchpad;
				int palm_check_setting;
				int disable_for_ext_mouse;

				len = read(touchpad_notify_fd, buff, buff_len);
				if (len < 0) {
					perror("read notify fd");
					continue;
				}

				event = (struct inotify_event *)buff;

				if (strncmp(event->name, "syntouchpad", event->len))
					continue;

				if (read_touchpad_data_file(&palm_check_setting, &enable_click, &enable_touchpad, &disable_for_ext_mouse) < 0)
					continue;

				for (i = 0; i < touchpads_index; ++i) {
					update_enable_click_setting(&touchpads[i],
									 enable_click);
					update_palm_check_setting(&touchpads[i],
								palm_check_setting);
					update_enable_touchpad_setting(&touchpads[i],
								enable_touchpad);
				}
			}

			for (i = 0; i < keyboards_index; ++i) {
				if (FD_ISSET(keyboards[i], &fds)) {
					struct input_event event;
					data_read = read(keyboards[i], &event, sizeof(event));
					if (data_read > 0 && event.type == EV_KEY) {
						if (event.value)
							should_suppress = 1;
					}
				}
			}

			if (should_suppress) {
				struct timespec suppress_time;
				clock_gettime(CLOCK_MONOTONIC, &suppress_time);
				for (i = 0; i < touchpads_index; ++i) {
					if (touchpads[i].timeout != 0) {
						touchpads[i].suppress_time = suppress_time;
						if (write(touchpads[i].suppress_fd,
							&cmd_suppress_on, 1))
						{
							touchpads[i].suppressed = 1;
						}
					}
				}
			}
		}

		suppress_count = 0;
		for (i = 0; i < touchpads_index; i++) {
			if (touchpads[i].suppressed) {
				if (diff_time(&touchpads[i].suppress_time)
					> touchpads[i].timeout)
				{
					int enable_click;
					int enable_touchpad;
					int palm_check_setting;
					int disable_for_ext_mouse;
					read_touchpad_data_file(&palm_check_setting, &enable_click, &enable_touchpad, &disable_for_ext_mouse);
					if(enable_touchpad)
					{
						if (write(touchpads[i].suppress_fd, &cmd_suppress_off, 1)) {
							touchpads[i].suppressed = 0;
						}
					}
				}
				else
					++suppress_count;
			}
		}

		if (suppress_count > 0) {
			select_timeout.tv_sec = 0;
			select_timeout.tv_usec = 50000;
			pselect_timeout = &select_timeout;
		} else
			pselect_timeout = NULL;
	}

	return 0;
}
