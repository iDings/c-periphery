#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#include "pwm.h"

#define P_PATH_MAX  256
/* Delay between checks for successful GPIO export (100ms) */
#define PWM_EXPORT_STAT_DELAY      100000
/* Number of retries to check for successful GPIO exports */
#define PWM_EXPORT_STAT_RETRIES    10

static int _pwm_error(struct pwm_handle *pwm, int code, int c_errno, const char *fmt, ...) {
    va_list ap;

    pwm->error.c_errno = c_errno;

    va_start(ap, fmt);
    vsnprintf(pwm->error.errmsg, sizeof(pwm->error.errmsg), fmt, ap);
    va_end(ap);

    /* Tack on strerror() and errno */
    if (c_errno) {
        char buf[64];
        strerror_r(c_errno, buf, sizeof(buf));
        snprintf(pwm->error.errmsg+strlen(pwm->error.errmsg), sizeof(pwm->error.errmsg)-strlen(pwm->error.errmsg), ": %s [errno %d]", buf, c_errno);
    }

    return code;
}

int pwm_open(pwm_t *pwm, unsigned int pwm_chip_id, unsigned int pwm_id) {
	char pwm_path[P_PATH_MAX];
	struct stat stat_buf;
	char buf[16];
	int fd;

	if (stat("/sys/class/pwm", &stat_buf) < 0)
		return _pwm_error(pwm, PWM_ERROR_EXPORT, errno, "PWM sysfs not enabled in kernel");

	/* Check whether pwmchip id is valid */
	snprintf(pwm_path, sizeof(pwm_path), "/sys/class/pwm/pwmchip%d", pwm_chip_id);
	if (stat(pwm_path, &stat_buf) < 0)
		return _pwm_error(pwm, PWM_ERROR_EXPORT, errno, "PWM chip id is invalid");

	snprintf(pwm_path, sizeof(pwm_path), "/sys/class/pwm/pwmchip%d/pwm%d", pwm_chip_id, pwm_id);
	if (stat(pwm_path, &stat_buf) < 0) {
		snprintf(pwm_path, sizeof(pwm_path), "/sys/class/pwm/pwmchip%d/export", pwm_chip_id);
		snprintf(buf, sizeof(buf), "%u", pwm_id);
		if ((fd = open(pwm_path, O_WRONLY)) < 0)
			return _pwm_error(pwm, PWM_ERROR_EXPORT, errno, "Exporting PWM: opening 'export'");
		if (write(fd, buf, strlen(buf)+1) < 0) {
			int errsv = errno;
			close(fd);
			return _pwm_error(pwm, PWM_ERROR_EXPORT, errsv, "Exporting PWM: writing 'export'");
		}
		if (close(fd) < 0)
			return _pwm_error(pwm, PWM_ERROR_EXPORT, errno, "Exporting PWM: closing 'export'");

		/* Wait until PWM direction appears */
		unsigned int retry_count;
		snprintf(pwm_path, sizeof(pwm_path), "/sys/class/pwm/pwmchip%d/pwm%d", pwm_chip_id, pwm_id);
		for (retry_count = 0; retry_count < PWM_EXPORT_STAT_RETRIES; retry_count++) {
			int ret = stat(pwm_path, &stat_buf);
			if (ret == 0)
				break;
			else if (ret < 0 && errno != ENOENT)
				return _pwm_error(pwm, PWM_ERROR_EXPORT, errno, "Exporting PWM: stat 'pwm%d/'", pwm_id);

			usleep(PWM_EXPORT_STAT_DELAY);
		}

		if (retry_count == PWM_EXPORT_STAT_RETRIES)
			return _pwm_error(pwm, PWM_ERROR_EXPORT, 0, "Exporting PWM: waiting for 'pwmchi%d/pwm%d' timed out", pwm_chip_id, pwm_id);
	}

	memset(pwm, 0, sizeof(struct pwm_handle));
	pwm->pwm_chip_id = pwm_chip_id;
	pwm->pwm_id = pwm_id;

	return 0;
}

int pwm_config(pwm_t *pwm, int period_ns, int duty_ns, bool enable) {
	int ret;
	bool old_enable;

	ret = pwm_get_enable(pwm, &old_enable);
	if (ret < 0)
		return ret;

	/* stop when config pwm */
	if (old_enable) {
		ret = pwm_set_enable(pwm, false);
		if (ret < 0)
			return ret;
	}

	/* first set period */
	ret = pwm_set_period(pwm, period_ns);
	if (ret < 0)
		return ret;

	ret = pwm_set_duty(pwm, duty_ns);
	if (ret < 0)
		return ret;

	if (enable) {
		ret = pwm_set_enable(pwm, enable);
		if (ret < 0)
			return ret;
	}

	return 0;
}

int pwm_set_enable(pwm_t *pwm, bool enable) {
	char pwm_path[P_PATH_MAX];
	char buf[2];
	int fd;

	snprintf(pwm_path, sizeof(pwm_path), "/sys/class/pwm/pwmchip%d/pwm%d/enable",
			pwm->pwm_chip_id, pwm->pwm_id);
	if ((fd = open(pwm_path, O_WRONLY)) < 0)
		return _pwm_error(pwm, PWM_ERROR_SET_ENABLE, errno, "Opening PWM 'enable'");

	snprintf(buf, sizeof(buf), "%d", enable);
	if (write(fd, buf, strlen(buf)+1) < 0) {
		int errsv = errno;
		close(fd);
		return _pwm_error(pwm, PWM_ERROR_SET_ENABLE, errsv, "Writing PWM 'enable'");
	}
	if (close(fd) < 0)
		return _pwm_error(pwm, PWM_ERROR_SET_ENABLE, errno, "Closing PWM 'enable'");

	return 0;
}

int pwm_set_duty(pwm_t *pwm, int duty_ns) {
	char pwm_path[P_PATH_MAX];
	char buf[32];
	int fd;

	snprintf(pwm_path, sizeof(pwm_path), "/sys/class/pwm/pwmchip%d/pwm%d/duty_cycle",
			pwm->pwm_chip_id, pwm->pwm_id);
	if ((fd = open(pwm_path, O_WRONLY)) < 0)
		return _pwm_error(pwm, PWM_ERROR_SET_DUTY, errno, "Opening PWM 'duty_cycle'");

	snprintf(buf, sizeof(buf), "%d", duty_ns);
	if (write(fd, buf, strlen(buf)+1) < 0) {
		int errsv = errno;
		close(fd);
		return _pwm_error(pwm, PWM_ERROR_SET_DUTY, errsv, "Writing PWM 'duty_cycle'");
	}
	if (close(fd) < 0)
		return _pwm_error(pwm, PWM_ERROR_SET_DUTY, errno, "Closing PWM 'duty_cycle'");

	return 0;
}

int pwm_set_period(pwm_t *pwm, int period_ns) {
	char pwm_path[P_PATH_MAX];
	char buf[32];
	int fd;

	snprintf(pwm_path, sizeof(pwm_path), "/sys/class/pwm/pwmchip%d/pwm%d/period",
			pwm->pwm_chip_id, pwm->pwm_id);
	if ((fd = open(pwm_path, O_WRONLY)) < 0)
		return _pwm_error(pwm, PWM_ERROR_SET_PERIOD, errno, "Opening PWM 'period'");

	snprintf(buf, sizeof(buf), "%d", period_ns);
	if (write(fd, buf, strlen(buf)+1) < 0) {
		int errsv = errno;
		close(fd);
		return _pwm_error(pwm, PWM_ERROR_SET_PERIOD, errsv, "Writing PWM 'period'");
	}
	if (close(fd) < 0)
		return _pwm_error(pwm, PWM_ERROR_SET_PERIOD, errno, "Closing PWM 'period'");

	return 0;
}

int pwm_get_enable(pwm_t *pwm, bool *enable) {
	char pwm_path[P_PATH_MAX];
	char buf[2];
	int fd;

	if (!enable)
		return _pwm_error(pwm, PWM_ERROR_GET_DUTY, 0, "Invlid parameter");

	snprintf(pwm_path, sizeof(pwm_path), "/sys/class/pwm/pwmchip%d/pwm%d/enable",
			pwm->pwm_chip_id, pwm->pwm_id);
	if ((fd = open(pwm_path, O_RDONLY)) < 0)
		return _pwm_error(pwm, PWM_ERROR_GET_ENABLE, errno, "Opening PWM 'enable'");
	if (read(fd, buf, 2) < 0) {
		int errsv = errno;
		close(fd);
		return _pwm_error(pwm, PWM_ERROR_GET_ENABLE, errsv, "Reading PWM 'enable'");
	}
	if (close(fd) < 0)
		return _pwm_error(pwm, PWM_ERROR_GET_ENABLE, errno, "Closing PWM 'enable'");

	if (buf[0] == '0')
		*enable = false;
	else if (buf[1] == '1')
		*enable = true;
	else
		return _pwm_error(pwm, PWM_ERROR_GET_ENABLE, 0, "Unkown PWM enable");

	return 0;
}

int pwm_get_duty(pwm_t *pwm, int *duty_ns) {
	char pwm_path[P_PATH_MAX];
	char buf[32];
	int fd, ret;

	if (!duty_ns)
		return _pwm_error(pwm, PWM_ERROR_GET_DUTY, 0, "Invlid parameter");

	snprintf(pwm_path, sizeof(pwm_path), "/sys/class/pwm/pwmchip%u/pwm%u/duty_cycle",
			pwm->pwm_chip_id, pwm->pwm_id);
	fd = open(pwm_path, O_RDONLY);
	if (fd < 0)
		return _pwm_error(pwm, PWM_ERROR_GET_DUTY, errno, "Opening PWM 'duty_cycle'");
	do {
		ret = read(fd, buf, sizeof(buf)-1);
	} while(ret < 0 && ret == -EINTR);
	if (ret < 0) {
		int errsv = errno;
		close(fd);
		return _pwm_error(pwm, PWM_ERROR_GET_DUTY, errsv, "Reading PWM 'duty_cycle'");
	}

	if (close(fd) < 0)
		return _pwm_error(pwm, PWM_ERROR_GET_DUTY, errno, "Closing PWM 'duty_cycle'");

	buf[ret] = '\0';

	ret = sscanf(buf, "%d\n", duty_ns);
	if (ret != 1)
		return _pwm_error(pwm, PWM_ERROR_GET_DUTY, 0, "Getting PWM 'duty_cycle' invalid");

	return 0;
}

int pwm_get_period(pwm_t *pwm, int *period_ns) {
	char pwm_path[P_PATH_MAX];
	char buf[32];
	int fd, ret;

	if (!period_ns)
		return _pwm_error(pwm, PWM_ERROR_GET_PERIOD, 0, "Invlid parameter");

	snprintf(pwm_path, sizeof(pwm_path), "/sys/class/pwm/pwmchip%u/pwm%u/period",
			pwm->pwm_chip_id, pwm->pwm_id);
	fd = open(pwm_path, O_RDONLY);
	if (fd < 0)
		return _pwm_error(pwm, PWM_ERROR_GET_PERIOD, errno, "Opening PWM 'period'");
	do {
		ret = read(fd, buf, sizeof(buf)-1);
	} while(ret < 0 && ret == -EINTR);
	if (ret < 0) {
		int errsv = errno;
		close(fd);
		return _pwm_error(pwm, PWM_ERROR_GET_PERIOD, errsv, "Reading PWM 'period'");
	}

	if (close(fd) < 0)
		return _pwm_error(pwm, PWM_ERROR_GET_PERIOD, errno, "Closing PWM 'period'");

	buf[ret] = '\0';

	ret = sscanf(buf, "%d\n", period_ns);
	if (ret != 1)
		return _pwm_error(pwm, PWM_ERROR_GET_PERIOD, 0, "Getting PWM 'period' invalid");

	return 0;

}

int pwm_tostring(pwm_t *pwm, char *str, size_t len) {
	bool enable = false;
	int duty_ns = -1;
	int period_ns = -1;

	pwm_get_enable(pwm, &enable);
	pwm_get_duty(pwm, &duty_ns);
	pwm_get_period(pwm, &period_ns);

	return snprintf(str, len, "PWM pwmchip%u/pwm%u (duty_ns=%d, period_ns=%d, enable=%d)",
			pwm->pwm_chip_id, pwm->pwm_id,duty_ns, period_ns, enable);
}

int pwm_errno(pwm_t *pwm) {
	return pwm->error.c_errno;
}

const char *pwm_errmsg(pwm_t *pwm) {
	return pwm->error.errmsg;
}
