#ifndef _PERIPHERY_PWM_H
#define _PERIPHERY_PWM_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum pwm_errno_code {
	PWM_ERROR_EXPORT			= -1,
	PWM_ERROR_SET_ENABLE		= -2,
	PWM_ERROR_SET_DUTY			= -3,
	PWM_ERROR_SET_PERIOD		= -4,
	PWM_ERROR_GET_ENABLE		= -5,
	PWM_ERROR_GET_DUTY			= -6,
	PWM_ERROR_GET_PERIOD		= -7,
};

typedef struct pwm_handle {
	unsigned int pwm_chip_id;
	unsigned int pwm_id;

	struct {
		int c_errno;
		char errmsg[96];
	} error;
} pwm_t;

/* Primary Functions */
int pwm_open(pwm_t *pwm, unsigned int pwm_chip_id, unsigned int pwm_id);
int pwm_config(pwm_t *pwm, int period_ns, int duty_ns, bool enable);

/* Setters */
int pwm_set_enable(pwm_t *pwm, bool enable);
int pwm_set_duty(pwm_t *pwm, int duty_ns);
int pwm_set_period(pwm_t *pwm, int period_ns);

/* Getters */
int pwm_get_enable(pwm_t *pwm, bool *enable);
int pwm_get_duty(pwm_t *pwm, int *duty_ns);
int pwm_get_period(pwm_t *pwm, int *period_ns);

/* Miscellaneous */
int pwm_tostring(pwm_t *pwm, char *str, size_t len);

/* Error Handling */
int pwm_errno(pwm_t *pwm);
const char *pwm_errmsg(pwm_t *pwm);

#ifdef __cplusplus
}
#endif

#endif
