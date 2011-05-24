/*
 * $Id$
 *
 * Copyright (C) 2008-2009 Antoine Drouin <poinix@gmail.com>
 *
 * This file is part of paparazzi.
 *
 * paparazzi is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * paparazzi is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with paparazzi; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/** \file stabilization_attitude_ref_int.c
 *  \brief Booz attitude reference generation (quaternion int version)
 *
 */

#include "generated/airframe.h"
#include "firmwares/rotorcraft/stabilization.h"
#include "subsystems/ahrs.h"

#include "stabilization_attitude_ref_int.h"
//#include "quat_setpoint.h"

#define REF_ACCEL_MAX_P STABILIZATION_ATTITUDE_REF_MAX_PDOT
#define REF_ACCEL_MAX_Q STABILIZATION_ATTITUDE_REF_MAX_QDOT
#define REF_ACCEL_MAX_R STABILIZATION_ATTITUDE_REF_MAX_RDOT

struct Int32Eulers stab_att_sp_euler;
struct Int32Quat   stab_att_sp_quat;
struct Int32Eulers stab_att_ref_euler;
struct Int32Quat   stab_att_ref_quat;
struct Int32Rates  stab_att_ref_rate;
struct Int32Rates  stab_att_ref_accel;

//struct Int32RefModel stab_att_ref_model[STABILIZATION_ATTITUDE_GAIN_NB];
struct Int32RefModel stab_att_ref_model = {
  {STABILIZATION_ATTITUDE_REF_OMEGA_P, STABILIZATION_ATTITUDE_REF_OMEGA_Q, STABILIZATION_ATTITUDE_REF_OMEGA_R},
  {STABILIZATION_ATTITUDE_REF_ZETA_P, STABILIZATION_ATTITUDE_REF_ZETA_Q, STABILIZATION_ATTITUDE_REF_ZETA_R}
};

//static int ref_idx = STABILIZATION_ATTITUDE_GAIN_IDX_DEFAULT;

/*
static const float omega_p[] = STABILIZATION_ATTITUDE_REF_OMEGA_P;
static const float zeta_p[] = STABILIZATION_ATTITUDE_REF_ZETA_P;
static const float omega_q[] = STABILIZATION_ATTITUDE_REF_OMEGA_Q;
static const float zeta_q[] = STABILIZATION_ATTITUDE_REF_ZETA_Q;
static const float omega_r[] = STABILIZATION_ATTITUDE_REF_OMEGA_R;
static const float zeta_r[] = STABILIZATION_ATTITUDE_REF_ZETA_R;
*/

static void reset_psi_ref_from_body(void) {
    stab_att_ref_euler.psi = ahrs.ltp_to_body_euler.psi;
    stab_att_ref_rate.r = 0;
    stab_att_ref_accel.r = 0;
}

static void update_ref_quat_from_eulers(void) {
    struct Int32RMat ref_rmat;

#ifdef STICKS_RMAT312
    INT32_RMAT_OF_EULERS_312(ref_rmat, stab_att_ref_euler);
#else
    INT32_RMAT_OF_EULERS_321(ref_rmat, stab_att_ref_euler);
#endif
    INT32_QUAT_OF_RMAT(stab_att_ref_quat, ref_rmat);
    INT32_QUAT_WRAP_SHORTEST(stab_att_ref_quat);
}

void stabilization_attitude_ref_init(void) {

  INT_EULERS_ZERO(stab_att_sp_euler);
  INT32_QUAT_ZERO(  stab_att_sp_quat);
  INT_EULERS_ZERO(stab_att_ref_euler);
  INT32_QUAT_ZERO(  stab_att_ref_quat);
  INT_RATES_ZERO( stab_att_ref_rate);
  INT_RATES_ZERO( stab_att_ref_accel);

  /*
  for (int i = 0; i < STABILIZATION_ATTITUDE_GAIN_NB; i++) {
    RATES_ASSIGN(stab_att_ref_model[i].omega, omega_p[i], omega_q[i], omega_r[i]);
    RATES_ASSIGN(stab_att_ref_model[i].zeta, zeta_p[i], zeta_q[i], zeta_r[i]);
  }
  */

}

void stabilization_attitude_ref_schedule(uint8_t idx)
{
  //ref_idx = idx;
}

void stabilization_attitude_ref_enter()
{
  reset_psi_ref_from_body();
  //stabilization_attitude_sp_enter();
  update_ref_quat_from_eulers();
}

/*
 * Reference
 */
#ifdef BOOZ_AP_PERIODIC_PRESCALE
#define DT_UPDATE ((float) BOOZ_AP_PERIODIC_PRESCALE / (float) PERIODIC_FREQ)
#else
#define DT_UPDATE (1./512.)
#endif

#include "messages.h"
#include "mcu_periph/uart.h"
#include "downlink.h"

void stabilization_attitude_ref_update() {

  static uint8_t x = 0;
  int32_t y = 0;

  if (++x > 10) {
    x = 0;
  DOWNLINK_SEND_BOOZ2_AHRS_QUAT(DefaultChannel, &stab_att_sp_quat.qi, &stab_att_sp_quat.qx, &stab_att_sp_quat.qy, &stab_att_sp_quat.qz, &ahrs.ltp_to_body_quat.qi, &ahrs.ltp_to_body_quat.qx, &ahrs.ltp_to_body_quat.qy, &ahrs.ltp_to_body_quat.qz);
  } 

  /* integrate reference attitude            */
  struct Int32Quat qdot;
  INT32_QUAT_DERIVATIVE(qdot, stab_att_ref_rate, stab_att_ref_quat);
  QUAT_SMUL(qdot, qdot, RATE_BFP_OF_REAL(DT_UPDATE));
  QUAT_ADD(stab_att_ref_quat, qdot);
  INT32_QUAT_NORMALIZE(stab_att_ref_quat);

  /* integrate reference rotational speeds   */
  struct Int32Rates delta_rate;
  RATES_SMUL(delta_rate, stab_att_ref_accel, RATE_BFP_OF_REAL(DT_UPDATE));
  RATES_ADD(stab_att_ref_rate, delta_rate);

  /* compute reference angular accelerations */
  struct Int32Quat err;
  /* compute reference attitude error        */
  INT32_QUAT_INV_COMP(err, stab_att_sp_quat, stab_att_ref_quat);
  /* wrap it in the shortest direction       */
  INT32_QUAT_WRAP_SHORTEST(err);
  /* propagate the 2nd order linear model    */
  stab_att_ref_accel.p = -2.*stab_att_ref_model.zeta.p*stab_att_ref_model.omega.p*stab_att_ref_rate.p
    - stab_att_ref_model.omega.p*stab_att_ref_model.omega.p*err.qx;
  stab_att_ref_accel.q = -2.*stab_att_ref_model.zeta.q*stab_att_ref_model.omega.q*stab_att_ref_rate.q
    - stab_att_ref_model.omega.q*stab_att_ref_model.omega.q*err.qy;
  stab_att_ref_accel.r = -2.*stab_att_ref_model.zeta.r*stab_att_ref_model.omega.r*stab_att_ref_rate.r
    - stab_att_ref_model.omega.r*stab_att_ref_model.omega.r*err.qz;

  /*	saturate acceleration */
  const struct Int32Rates MIN_ACCEL = { -REF_ACCEL_MAX_P, -REF_ACCEL_MAX_Q, -REF_ACCEL_MAX_R };
  const struct Int32Rates MAX_ACCEL = {  REF_ACCEL_MAX_P,  REF_ACCEL_MAX_Q,  REF_ACCEL_MAX_R };
  RATES_BOUND_BOX(stab_att_ref_accel, MIN_ACCEL, MAX_ACCEL);

  /* compute ref_euler */
  INT32_EULERS_OF_QUAT(stab_att_ref_euler, stab_att_ref_quat);

}