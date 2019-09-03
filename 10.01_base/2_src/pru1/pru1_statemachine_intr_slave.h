/* pru1_statemachine_intr_slave.h: CPU receives interrupt vector

 Copyright (c) 2018-2019, Joerg Hoppe
 j_hoppe@t-online.de, www.retrocmp.com

 Permission is hereby granted, free of charge, to any person obtaining a
 copy of this software and associated documentation files (the "Software"),
 to deal in the Software without restriction, including without limitation
 the rights to use, copy, modify, merge, publish, distribute, sublicense,
 and/or sell copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 JOERG HOPPE BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


 26-aug-2019	JH	start
 */
#ifndef  _PRU1_STATEMACHINE_INTR_SLAVE_H_
#define  _PRU1_STATEMACHINE_INTR_SLAVE_H_

#include "pru1_utils.h"	// statemachine_state_func

typedef struct {
	uint16_t vector; // interrupt vector to transfer
	uint8_t level_index; // 0..3 = BR..BR7. to be returned to ARM on complete
} statemachine_intr_slave_t;

extern statemachine_intr_slave_t sm_intr_slave;

statemachine_state_func sm_intr_slave_start(void);

#endif
