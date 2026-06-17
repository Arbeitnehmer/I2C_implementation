
/*
 * @brief Implementation of alsm module.
 *
 * This module supports the ROHM Semiconductor BH1750FVI Ambient Light sensor.
 * Sensor has an I2C interface.
 *
 * This module polls the sensor at a configurable rate (e.g. once every 1000
 * ms). The module can can be queried at any time for the last measurement,
 * including the "age" (in ms) of that measurement.
 *
 * The following console commands are provided:
 * > alsm status
 * > alsm pm
 * > alsm log
 * > alsm test
 * See code for details.
 *
 * @ref https://cdn-reichelt.de/documents/datenblatt/A300/SENSOR_BH1750.pdf



*/
#include "tmr.h"
#include "log.h"


#include "alsm.h"




////////////////////////////////////////////////////////////////////////////////
// Common macros
////////////////////////////////////////////////////////////////////////////////

//sensor addr in case addr_pin -> Low
#define I2C_ALSM_LOW_ADDR 0x23

////////////////////////////////////////////////////////////////////////////////
// Type definitions
////////////////////////////////////////////////////////////////////////////////

enum states {
    STATE_IDLE,
	STATE_READ_MEAS_VALUE,
	STATE_WAIT_MEAS
};

#define I2C_MSG_BFR_LEN 2	//in bytes, 2bytes per i2c read.

// state information of module
struct alsm_state {
    struct alsm_cfg cfg;
    int32_t tmr_id;
    uint32_t i2c_op_start_ms;
    uint32_t last_meas_ms;
    uint8_t msg_bfr[I2C_MSG_BFR_LEN];

    bool got_meas;
    enum states state;

    bool log_meas_cli;
    //measurement data of the module
    //are part of the interface.
    int32_t last_meas;
    uint32_t max_meas;
    uint32_t min_meas;
    uint32_t mean_meas;
};


// Performance measurements for i2c. Currently these are common to all
// instances.  A future enhancement would be to make them per-instance.

enum alsm_u16_pms {
    CNT_WRITE_INIT_FAIL,
    CNT_WRITE_OP_FAIL,
    CNT_READ_INIT_FAIL,
    CNT_READ_OP_FAIL,
    CNT_TASK_OVERRUN,

    NUM_U16_PMS
};

////////////////////////////////////////////////////////////////////////////////
// Private (static) function declarations
////////////////////////////////////////////////////////////////////////////////

static enum tmr_cb_action tmr_callback(int32_t tmr_id, uint32_t user_data);

////////////////////////////////////////////////////////////////////////////////
// Private (static) variables
////////////////////////////////////////////////////////////////////////////////

static struct alsm_state alsm_state;

static int32_t log_level = LOG_INFO;

// Storage for performance measurements.
static uint16_t cnts_u16[NUM_U16_PMS];

// Names of performance measurements.
static const char* cnts_u16_names[NUM_U16_PMS] = {
    "write init fail",
    "write op fail",
    "read init fail",
    "read op fail",
    "task overrun",
};

////////////////////////////////////////////////////////////////////////////////
// Public (global) variables and externs
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Public (global) functions
////////////////////////////////////////////////////////////////////////////////

// Core module interface functions.

//write/read cfg data-strucutre.
int32_t alsm_get_def_cfg(struct alsm_cfg* cfg){
	return 0;
}

/*
 * @brief Initialize alsm singleton-instance.
 *
 * @param[in] cfg The alsm configuration. (FUTURE)
 *
 * @return 0 for success, else a "MOD_ERR" value. See code for details.
 *
 * This function initializes the alsm singleton instance. Generally, it should not
 * access other modules as they might not have been initialized yet.  An
 * exception is the log module.
 */
int32_t alsm_init(struct alsm_cfg* cfg){
    alsm_state.cfg = *cfg;
	return 0;
}

//set-up connections to other modules, such as tmr, watchdog
//use i2c command to start the sensor.
//maybe also state machine needed?!
int32_t alsm_start(){
	struct alsm_state *st;
	st = &alsm_state;

    st->tmr_id = tmr_inst_get_cb(st->cfg.sample_time_ms, tmr_callback,
                                 0, TMR_CNTX_BASE_LEVEL);

    if (st->tmr_id < 0){
    	//using tmr failed, maybe log-error.
    	//module not usable for periodic sensor reading.
    	return st->tmr_id;
    }

	return 0;
}


//function which does the heavy lifting.
//read data via i2c, then do the post-processing
//then do the post-processing
//store the result or print via CLI
//implement via state machine?!
int32_t alsm_run(){
	return 0;
}




////////////////////////////////////////////////////////////////////////////////
// Private (static) functions
////////////////////////////////////////////////////////////////////////////////
/*
 * @brief tmr_callback.
 *
 * @param[in]  instance_id Identifies the tmphm instance.
 * @param[out] meas Measurement structure to fill in.
 * @param[out] meas_age_ms Variable to place measurement age in ms (or NULL).
 *
 * start state machine by transition idle->read_meas_value
 * alsm_run() handles rest of state machine.
 *
 * @return TMR_CB_RESTART so this function gets called agian by tmr module.
 *
 * Concerns: Is this information leakage of state-machine, between tmr_cb_action() and run()?!
 */
static enum tmr_cb_action tmr_callback(int32_t tmr_id, uint32_t user_data){
	struct alsm_state* st;

    log_verbose("tmr_callback(tmr_id=%d user_data=%lu)\n", tmr_id, user_data);


    st = &alsm_state;

    if(st->state == STATE_IDLE){
    	//initiate next sensor read via state machine
    	st->state = STATE_READ_MEAS_VALUE;

    }else{
    	//module still in another state, measurement period too fast and/or error occured.
    	//add LWL
    	INC_SAT_U16(cnts_u16[CNT_TASK_OVERRUN]);
    }


	return TMR_CB_RESTART;
}

