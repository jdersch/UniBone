/* unibusadapter.hpp: connects multiple "unibusdevices" to the PRU UNIBUS interface

 Copyright (c) 2018-2019 Joerg Hoppe
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


 jul-2019     JH      rewrite: multiple parallel arbitration levels	 
 12-nov-2018  JH      entered beta phase

 */
#ifndef _UNIBUSADAPTER_HPP_
#define _UNIBUSADAPTER_HPP_

#include "iopageregister.h"
#include "priorityrequest.hpp"
#include "unibusadapter.hpp"
#include "unibusdevice.hpp"

// for each priority arbitration level, theres a table with backplane slots.
//  Each device sits in a slot, the slot determinss the request priority within one level (BR4567,NP).
class priority_request_level_c {
public:
	// remember for each backplane slot wether the device has requested
	// INTR or DMA at this level
	priority_request_c* slot_request[PRIORITY_SLOT_COUNT + 1];
	// Optimization to find the high priorized slot in use very fast.
	// bit array: bit set -> slot<bitnr> has open request.
	uint32_t slot_request_mask;

	priority_request_c* active; // request currently handled by PRU, not in table anymore

	void clear();
};

// is a device_c. need a thread (but no params)
class unibusadapter_c: public device_c {
private:

	// handle arbitration for each of the 5 device request levels in parallel
	priority_request_level_c request_levels[PRIORITY_LEVEL_COUNT];

	// access of master CPU to memory not handled via priority arbitration
	dma_request_c 	*cpu_data_transfer_request ; // needs no link to CPU

	pthread_mutex_t requests_mutex;

	void worker_init_event(void);
	void worker_power_event(bool power_down);
	void worker_deviceregister_event(void);
	void worker_dma_chunk_complete_event(bool cpu_DATA_transfer);
	void worker_intr_complete_event(uint8_t level_index);
	void worker(unsigned instance) override; // background worker function

public:
	unibusadapter_c();

	bool on_param_changed(parameter_c *param) override;  // must implement

	// list of registered devices.
	// Defines GRANT priority:
	// Lower index = "nearer to CPU" = higher priority
	unibusdevice_c *devices[MAX_DEVICE_HANDLE + 1];

	volatile bool line_INIT; // current state of these UNIBUS signals
	volatile bool line_DCLO;
	volatile bool line_ACLO;

	void on_power_changed(void) override; // must implement
	void on_init_changed(void) override; // must implement

	bool register_device(unibusdevice_c& device);
	void unregister_device(unibusdevice_c& device);

	// Helper for request processing
	void requests_init(void);

	void request_schedule(priority_request_c& request);
	void requests_cancel_scheduled(void);
	priority_request_c *request_activate_lowest_slot(unsigned level_index);
//	bool request_is_active(		unsigned level_index);
	bool request_is_blocking_active(uint8_t level_index);
	void request_active_complete(unsigned level_index);
	void request_execute_active_on_PRU(unsigned level_index);

	void DMA(dma_request_c& dma_request, bool blocking, uint8_t unibus_control,
			uint32_t unibus_addr, uint16_t *buffer, uint32_t wordcount);
	void INTR(intr_request_c& intr_request, unibusdevice_register_t *interrupt_register,
			uint16_t interrupt_register_value);
	void cancel_INTR(intr_request_c& intr_request);

	void cpu_DATA_transfer(dma_request_c& dma_request, uint8_t unibus_control, uint32_t unibus_addr, uint16_t *buffer);

	void print_shared_register_map(void);

};

extern unibusadapter_c *unibusadapter; // another Singleton

#endif
