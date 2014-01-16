/*
 *
 *  oFono - Open Source Telephony
 *
 *  Copyright (C) 2009-2010 Nokia Corporation and/or its subsidiary(-ies).
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#ifndef __OFONO_CELL_INFO_H
#define __OFONO_CELL_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <ofono/types.h>
#include <stdint.h>

struct ofono_cell_info;


#define OFONO_CI_FIELD_TA_UNDEFINED 0xFF
#define OFONO_CI_FIELD_UCID_UNDEFINED 0xFFFFFFFF
#define OFONO_CI_FIELD_ECN0_UNDEFINED 0xFF
#define OFONO_CI_FIELD_RSCP_UNDEFINED -127
#define OFONO_CI_FIELD_PATHLOSS_UNDEFINED 0xFF
#define OFONO_CI_FIELD_FREQ_UNDEFINED 0xFFFF

#define OFONO_MAX_NMR_COUNT 15
#define OFONO_MAX_MEASURED_CELL_COUNT 32
#define OFONO_MAX_MEAS_RES_LIST_COUNT 8

struct geran {
	uint16_t lac;
	uint16_t ci;
	uint8_t ta;
	uint8_t no_cells;
	struct geran_neigh_cell {
		uint16_t arfcn;
		uint8_t bsic;
		uint8_t rxlev;
	} nmr[OFONO_MAX_NMR_COUNT];
};

struct cell_measured_results {
	uint32_t ucid;
	uint16_t sc;
	uint8_t ecn0;
	int16_t rscp;
	uint8_t pathloss;
};

struct measured_results_list {
	uint16_t dl_freq;
	uint16_t ul_freq;
	uint8_t rssi;
	uint8_t no_cells;
	struct cell_measured_results cmr[OFONO_MAX_MEASURED_CELL_COUNT];
};

struct utran {
	uint32_t ucid;
	uint16_t dl_freq;
	uint16_t ul_freq;
	uint16_t sc;
	uint8_t no_freq;
	struct measured_results_list mrl[OFONO_MAX_MEAS_RES_LIST_COUNT];
};

struct ofono_cell_info_results {

	char mcc[OFONO_MAX_MCC_LENGTH + 1];
	char mnc[OFONO_MAX_MNC_LENGTH + 1];

	gboolean has_geran_cells;
	gboolean has_utran_cells;

	struct geran geran;
	struct utran utran;

};


typedef void (*ofono_cell_info_query_cb_t)(const struct ofono_error *error,
	      struct ofono_cell_info_results *results, void *data);

struct ofono_cell_info_driver {
	const char *name;
	int (*probe)(struct ofono_cell_info *ci,
			unsigned int vendor,
			void *data);
	void (*remove)(struct ofono_cell_info *ci);

	void (*query)(struct ofono_cell_info *ci,
			ofono_cell_info_query_cb_t cb,
			void *data);
};

struct ofono_cell_info *ofono_cell_info_create(struct ofono_modem *modem,
						unsigned int vendor,
						const char *driver,
						void *data);

void ofono_cell_info_register(struct ofono_cell_info *ci);
void ofono_cell_info_remove(struct ofono_cell_info *ci);
int ofono_cell_info_driver_register(struct ofono_cell_info_driver *driver);
void ofono_cell_info_driver_unregister(struct ofono_cell_info_driver *driver);
void *ofono_cell_info_get_data(struct ofono_cell_info *ci);
void ofono_cell_info_set_data(struct ofono_cell_info *ci, void *cid);

#ifdef __cplusplus
}
#endif

#endif /* __OFONO_CELL_INFO_H */
