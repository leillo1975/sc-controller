/*
 * SC-Controller - Device Monitor - Common code
 *
 * Mostly common code for registering hotplug callbacks and related filtering.
 */
#define LOG_TAG "DevMon"
#include "scc/utils/logging.h"
#include "scc/utils/hashmap.h"
#include "scc/utils/assert.h"
#include "scc/utils/list.h"
#include "scc/input_device.h"
#include "daemon.h"
#include <stdarg.h>
#include <unistd.h>

typedef LIST_TYPE(HotplugFilter) Filters;

typedef struct {
	Subsystem			subsystem;
	sccd_hotplug_cb		callback;
	Filters				filters;
} CallbackData;

// Note: Following type has to have enought bits to fit all possible Subsystem enum values
static uint32_t enabled_subsystems = 0;
// static map_t callbacks;
static map_t known_devs;
static LIST_TYPE(CallbackData) callbacks;

/*
static char _key[256];
inline static const char* make_key(Subsystem sys, Vendor vendor, Product product) {
	sprintf(_key, "%i:%x:%x", sys, vendor, product);
	return _key;
}*/

static void free_callback_data(void* _data) {
	CallbackData* data = (CallbackData*)_data;
	list_free(data->filters);
	free(data);
}

void sccd_device_monitor_common_init() {
	callbacks = list_new(CallbackData, 10);
	known_devs = hashmap_new();
	ASSERT((callbacks != NULL) && (known_devs != NULL));
	list_set_dealloc_cb(callbacks, free_callback_data);
}

void sccd_device_monitor_close_common() {
	hashmap_free(known_devs);
	list_free(callbacks);
}


uint32_t sccd_device_monitor_get_enabled_subsystems(Daemon* d) {
	return enabled_subsystems;
}


void sccd_device_monitor_new_device(Daemon* d, const InputDeviceData* idata) {
	FOREACH_IN(CallbackData*, data, callbacks) {
		if (data->subsystem != idata->subsystem)
			continue;
		bool matches_filters = true;
		FOREACH_IN(HotplugFilter*, filter, data->filters) {
			if (!sccd_device_monitor_test_filter(d, idata, filter)) {
				matches_filters = false;
				break;
			}
		}
		if (matches_filters) {
			if (hashmap_put(known_devs, idata->path, (void*)1) != MAP_OK)	// OOM
				return;
			data->callback(d, idata);
			return;
		}
	}
	/*
	sccd_hotplug_cb cb = NULL;
	const char* key = make_key(sys, vendor, product);
	if (hashmap_get(callbacks, key, (any_t*)&cb) != MAP_MISSING) {
		// I have no value to store in known_devs hashmap yet.
		if (hashmap_put(known_devs, syspath, (void*)1) != MAP_OK)
			return;
		cb(d, syspath, sys, vendor, product, 0);
	}*/
}


void sccd_device_monitor_device_removed(Daemon* d, const char* syspath) {
	any_t trash;
	if (hashmap_get(known_devs, syspath, &trash) != MAP_MISSING) {
		DEBUG("Device '%s' removed", syspath);
		hashmap_remove(known_devs, syspath);
	}
}


bool sccd_register_hotplug_cb(Subsystem sys, sccd_hotplug_cb cb, const HotplugFilter* filters, ...) {
	CallbackData* data = malloc(sizeof(CallbackData));
	if (data == NULL) return false;
	data->filters = list_new(HotplugFilter, 1);
	if (data->filters == NULL) {
		free(data);
		return false;
	}
	list_set_dealloc_cb(data->filters, free);
	data->subsystem = sys;
	data->callback = cb;
	
	va_list ap;
	const HotplugFilter* filter = filters;
	va_start(ap, filters);
	while (filter != NULL) {
		HotplugFilter* copy = malloc(sizeof(HotplugFilter));
		if ((copy == NULL) || !list_add(data->filters, copy)) {
			// OOM
			va_end(ap);
			free(copy);
			free_callback_data(data);
			return false;
		}
		memcpy(copy, filter, sizeof(HotplugFilter));
		filter = va_arg(ap, const HotplugFilter*);
	}
	va_end(ap);
	
	if (!list_add(callbacks, data)) {
		free_callback_data(data);
		return false;
	}
	
	enabled_subsystems |= (1 << sys);
	return true;
}

