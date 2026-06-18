
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
//#include <>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>


#include "cmd.h"
#include "console.h"
#include "tmr.h"
#include "log.h"


#include "alsm.h"




////////////////////////////////////////////////////////////////////////////////
// Common macros
////////////////////////////////////////////////////////////////////////////////

//instructions via I2C
#define ALSM_Power_Down_I2C_Cmd 0x00U
#define ALSM_Power_On_I2C_Cmd 0x01U
#define ALSM_Reset_I2C_Cmd 0x07U
#define ALSM_Cont_H_Res_I2C_Cmd 0x10U
#define ALSM_Cont_H_Res2_I2C_Cmd 0x11U
#define ALSM_Cont_L_Res_I2C_Cmd 0x13U
#define ALSM_One_H_Res_I2C_Cmd 0x20U
#define ALSM_One_H_Res2_I2C_Cmd 0x21U
#define ALSM_One_L_Res_I2C_Cmd 0x23U

#define ALSM_Read_Bytes 2

#define ALSM_Wait_Time_MS 200

#if CONFIG_ALSM_ADDR_PIN == 0
//sensor addr in case addr_pin -> Low
	#define ALSM_I2C_ADDR 0x23U
#else
//sensor addr in case addr_pin -> HIGH
	#define ALSM_I2C_ADDR 0x5cU
#endif
////////////////////////////////////////////////////////////////////////////////
// Type definitions
////////////////////////////////////////////////////////////////////////////////

enum states {
    ALSM_STATE_IDLE,
	ALSM_STATE_READ_MEAS_VALUE,
	ALSM_STATE_WAIT_MEAS,
	ALSM_STATE_MEAS_PROC,
	ALSM_STATE_ERROR
};

#define I2C_MSG_BFR_LEN 2	//in bytes, 2bytes per i2c read.

// state information of module
struct alsm_state {
    struct alsm_cfg cfg;
    int32_t tmr_id;

    uint32_t i2c_op_start_ms;
    uint8_t msg_bfr[I2C_MSG_BFR_LEN];

    bool got_meas;
    enum states state;

    bool log_meas_cli;
    //measurement data of the module
    //are part of the interface.
    uint32_t last_lux_meas;
    uint32_t max_lux_meas;
    uint32_t min_lux_meas;
    int32_t mean_lux_meas;

    uint32_t last_meas_ms;
    uint32_t num_meas;
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

static int32_t cmd_alsm_status(int32_t argc, const char** argv);
static int32_t cmd_alsm_test(int32_t argc, const char** argv);

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


// Data structure with console command info.
static struct cmd_cmd_info cmds[] = {
    {
        .name = "status",
        .func = cmd_alsm_status,
        .help = "Get module status, usage: alsm status",
    },
    {
        .name = "test",
        .func = cmd_alsm_test,
        .help = "Run test, usage: alsm test [<op> [<arg>]] (enter no op for help)",
    }
};

// Data structure passed to cmd module for console interaction.
static struct cmd_client_info cmd_info = {
    .name = "alsm",
    .num_cmds = ARRAY_SIZE(cmds),
    .cmds = cmds,
    .log_level_ptr = &log_level,
    .num_u16_pms = NUM_U16_PMS,
    .u16_pms = cnts_u16,
    .u16_pm_names = cnts_u16_names,
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
    cfg->i2c_instance_id = CONFIG_ALSM_DFLT_I2C_INSTANCE;
    cfg->sample_time_ms = CONFIG_ALSM_DFLT_SAMPLE_TIME_MS;
    cfg->meas_time_ms = CONFIG_ALSM_DFLT_MEAS_TIME_MS;

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
	//state struct already initialied to 0 due to being a (static) global variable.
    alsm_state.cfg = *cfg;
    alsm_state.min_lux_meas = UINT32_MAX;
	return 0;
}

//set-up connections to other modules, such as tmr, watchdog
//use i2c command to start the sensor.
//maybe also state machine needed?!

/*
 * @brief start communicating with actual BH1750FVI sensor.
 *			set-up sensor for later on.
 *
 * @return 0 for success, else a "MOD_ERR" value. See code for details.
 *
 * sets up interaction with modules tmr and cmd.
 * Write Power-On Instruction to Sensor via I2C.
 * Write Continuously H-Resolution Mode Instruction to sensor so we can start reading out data.
 * There are also other modes available for this sensor, look into the Datas-Sheet.
 *
 */
int32_t alsm_start(){
	int32_t rc;
	struct alsm_state *st;
	st = &alsm_state;

    rc = cmd_register(&cmd_info);
    if (rc < 0) {
        log_error("alsm_start: cmd error %d\n", rc);
        return rc;
    }

    st->tmr_id = tmr_inst_get_cb(st->cfg.sample_time_ms, tmr_callback,
                                 0, TMR_CNTX_BASE_LEVEL);

    if (st->tmr_id < 0){
    	//using tmr failed, maybe log-error.
    	//module not usable for periodic sensor reading.
    	log_info("alsm_start: setting-up tmr_callback failed\n");
    	return st->tmr_id;
    }


    //turn on Sensor via I2C write.
    st->msg_bfr[0] = ALSM_Power_On_I2C_Cmd;
    i2c_write(st->cfg.i2c_instance_id, ALSM_I2C_ADDR, st->msg_bfr, 1);
    do{
    	rc = i2c_get_op_status(st->cfg.i2c_instance_id);
    }while(rc == MOD_ERR_OP_IN_PROG);

    if(rc==0){
    	log_debug("alsm_start: Sensor Power turned on success.\n");
    }else{
    	INC_SAT_U16(cnts_u16[CNT_WRITE_OP_FAIL]);
    	log_debug("alsm_start: Sensor Power on failed, rc=%" PRId32 "\n", rc);
    	st->state = ALSM_STATE_ERROR;
    	return rc;
    }

    //put Sensor in 'Continuously H-Resolution Mode'
    st->msg_bfr[0] = ALSM_Cont_H_Res_I2C_Cmd;
    i2c_write(st->cfg.i2c_instance_id, ALSM_I2C_ADDR, st->msg_bfr, 1);
    st->i2c_op_start_ms = tmr_get_ms();
    do{
    	rc = i2c_get_op_status(st->cfg.i2c_instance_id);
    }while(rc == MOD_ERR_OP_IN_PROG);

    if(rc==0){
    	log_debug("alsm_start: Sensor in continuous H-Reolution Mode.\n");
    }else{
    	INC_SAT_U16(cnts_u16[CNT_WRITE_OP_FAIL]);
    	log_debug("alsm_start: Sensor in continuous H-Reolution Mode failed, rc=%" PRId32 "\n", rc);
    	st->state = ALSM_STATE_ERROR;
    	return rc;
    }

    log_verbose("alsm_start: Successfuly set-up Module and Sensor\n");
    st->state = ALSM_STATE_IDLE;	//redundant but for clearity.
    return 0;
}


//function which does the heavy lifting.
//read data via i2c, then do the post-processing
//then do the post-processing
//store the result or print via CLI
//implement via state machine?!
int32_t alsm_run(){


    struct alsm_state* st;
    int32_t rc;

    st = &alsm_state;

    switch (st->state) {
        case ALSM_STATE_IDLE:
            break;
        case ALSM_STATE_READ_MEAS_VALUE:
        	if(tmr_get_ms() - st->i2c_op_start_ms > ALSM_Wait_Time_MS){
        		i2c_read(st->cfg.i2c_instance_id, ALSM_I2C_ADDR, st->msg_bfr, ALSM_Read_Bytes);
        		st->i2c_op_start_ms = tmr_get_ms();
        		st->state = ALSM_STATE_WAIT_MEAS;
        	}else{
        		//INC_SAT_U16(cnts_u16[CNT_TASK_OVERRUN]);
        	}
        	break;
        case ALSM_STATE_WAIT_MEAS:
        	rc = i2c_get_op_status(st->cfg.i2c_instance_id);
        	if(rc==0){	//no error.
        		st->state = ALSM_STATE_MEAS_PROC;
        	}else if(rc==MOD_ERR_OP_IN_PROG){
        		//stay in this state and wait for longer
        		//could get logged?!
        		//INC_SAT_U16(cnts_u16[CNT_TASK_OVERRUN]);
        	}else{
        		//some error occured, log error and return to IDLE
        		log_error("alsm_run: error reading sensor via i2c, rc=%d\n", rc);
        		st->state = ALSM_STATE_ERROR;
        		INC_SAT_U16(cnts_u16[CNT_READ_OP_FAIL]);
        	}
        	break;
        case ALSM_STATE_MEAS_PROC:
        	uint16_t data;
        	uint16_t lux_meas;
        	data = ((uint16_t)st->msg_bfr[0] << 8) | st->msg_bfr[1];
        	lux_meas = data/1.2f;
        	st->last_lux_meas = lux_meas;
        	st->last_meas_ms = tmr_get_ms();
        	st->num_meas++;

        	if(lux_meas < st->min_lux_meas){
        		st->min_lux_meas = lux_meas;
        	}
        	if(lux_meas > st->max_lux_meas){
        		st->max_lux_meas = lux_meas;
        	}

        	//running mean
        	int32_t delta;
        	delta = (int32_t)lux_meas - (int32_t)st->mean_lux_meas;
        	st->mean_lux_meas += delta / st->num_meas;


        	st->state = ALSM_STATE_IDLE;
        	if(st->log_meas_cli){
        		printc("alsm: AL %lu Lux\n", st->last_lux_meas);
        	}
        	break;
        case ALSM_STATE_ERROR:
        	break;
    }





	return 0;
}


/*
 * @brief get last sensor measurement.
 *
 * @param[out] meas the actual measurement value
 * @param[out] meas_age_ms how old is this measurement in ms.
 *
 * @return 0 for success, else a "MOD_ERR" value. See code for details.
 *
 */
int32_t alsm_get_last_meas(uint32_t* meas, uint32_t* meas_age_ms){

	struct alsm_state* st;
	st = &alsm_state;

	if(meas == NULL){
		return MOD_ERR_ARG;
	}
	if(st->num_meas == 0){
		return MOD_ERR_UNAVAIL;
	}

	*meas = st->last_lux_meas;
	if(meas_age_ms!=NULL){
		*meas_age_ms = tmr_get_ms() - st->last_meas_ms;
	}

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
 * @return TMR_CB_RESTART so this function gets called again by tmr module.
 *
 * Concerns: Is this information leakage of state-machine, between tmr_cb_action() and run()?!
 */
static enum tmr_cb_action tmr_callback(int32_t tmr_id, uint32_t user_data){
	struct alsm_state* st;

    log_verbose("tmr_callback(tmr_id=%d user_data=%lu)\n", tmr_id, user_data);


    st = &alsm_state;

    if(st->state == ALSM_STATE_IDLE){
    	//initiate next sensor read via state machine
    	st->state = ALSM_STATE_READ_MEAS_VALUE;
    }else if(st->state == ALSM_STATE_ERROR){
    	//module failed and is not functional.
    }else{
    	//module still processing a sensor read, measurement period too fast and/or error occured.
    	//add LWL
    	INC_SAT_U16(cnts_u16[CNT_TASK_OVERRUN]);
    }


	return TMR_CB_RESTART;
}


/*
 * @brief Console command function for "alsm status".
 *
 * @param[in] argc Number of arguments, including "alsm"
 * @param[in] argv Argument values, including "alsm"
 *
 * @return 0 for success, else a "MOD_ERR" value. See code for details.
 *
 * Command usage: alsm status
 */
static int32_t cmd_alsm_status(int32_t argc, const char** argv)
{
    struct alsm_state* st;
    st = &alsm_state;

    printc("      Num  Last Min  Max  Mean Log\n"
    	   "State Meas AL   AL   AL   AL   CLI\n"
    	   "----- ---- ---- ---- ---- ---- ---\n");
    printc("%d   %lu %lu %lu %lu %ld  %d\n", st->state,
    		st->num_meas, st->last_lux_meas,
			st->min_lux_meas, st->max_lux_meas, st->mean_lux_meas, st->log_meas_cli);
    return 0;
}

/*
 * @brief Console command function for "alsm test".
 *
 * @param[in] argc Number of arguments, including "alsm"
 * @param[in] argv Argument values, including "alsm"
 *
 * @return 0 for success, else a "MOD_ERR" value. See code for details.
 *
 * Command usage: alsm test [<op> [<arg>]]
 */
static int32_t cmd_alsm_test(int32_t argc, const char** argv)
{
    struct cmd_arg_val arg_vals[4];
    int32_t rc;

    // Handle help case.
    if (argc == 2) {
        printc("Test operations and param(s) are as follows:\n"
               "  Get last meas, usage: alsm test lastmeas\n"
               "  Set meas time, usage: alsm test meastime <time-ms>\n"
        	   "  Toggle cli-logging, usage: alsm test cli-log\n"
            );
        return 0;
    }

    if (argc < 3) {
        return MOD_ERR_BAD_CMD;
    }


    if (strcasecmp(argv[2], "lastmeas") == 0) {
        uint32_t meas;
        uint32_t meas_age_ms;
        rc = alsm_get_last_meas(&meas, &meas_age_ms);
        if (rc == 0)
            printf("AL=%lu Lux age=%lu ms\n",
                   meas,
                   meas_age_ms);
        else
            printf("alsm_get_last_meas fails rc=%ld\n", rc);
    } else if (strcasecmp(argv[2], "meastime") == 0) {
        rc = cmd_parse_args(argc-4, argv+4, "u", arg_vals);
        if (rc != 1) {
            return MOD_ERR_BAD_CMD;
        }
        alsm_state.cfg.meas_time_ms = arg_vals[0].val.u;
    } else if (strcasecmp(argv[2], "cli-log") == 0) {
    	alsm_state.log_meas_cli = !alsm_state.log_meas_cli;
    }
    else {
        printc("Invalid operation '%s'\n", argv[2]);
        return MOD_ERR_BAD_CMD;
    }

    return 0;
}

