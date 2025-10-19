/**
 * @file nvidia_powermizer.c
 * @author dqs105 (dqs105@dqsmcsrv.com)
 * @brief Powermizer control for NVIDIA GPUs
 * @version 0.1
 * @date 2025-03-14
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#include <getopt.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <chrono>
#include <memory>
#include <vector>

#include <nvml.h>

#define VERSION "0.2"

/* Logger */
typedef enum {
	LOG_DEBUG = 0,
	LOG_INFO,
	LOG_WARN,
	LOG_ERROR,
	LOG_FATAL
} LogLevel;

static LogLevel current_loglevel = LOG_INFO;

void log_printf(LogLevel level, const char *fmt, va_list args) {
	FILE *out = stdout;

	if (level < current_loglevel) {
		return;
	}

	switch (level) {
	case LOG_DEBUG:
		fprintf(stdout, "[DEBUG] ");
		break;
	case LOG_INFO:
		fprintf(stdout, "[INFO]  ");
		break;
	case LOG_WARN:
		fprintf(stderr, "[WARN]  ");
		out = stderr;
		break;
	case LOG_ERROR:
		fprintf(stderr, "[ERROR] ");
		out = stderr;
		break;
	case LOG_FATAL:
		fprintf(stderr, "[FATAL] ");
		out = stderr;
		break;
	default:
		break;
	}
	vfprintf(out, fmt, args);
	fprintf(out, "\n");
	fflush(out);
}

void log_printf(LogLevel level, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	log_printf(level, fmt, args);
	va_end(args);
}

/* Powermizer instance for a GPU */
class PowermizerInstance {
public:
	PowermizerInstance(int device_index, bool coder_enabled, 
		unsigned int boost_util, unsigned int low_power_util, unsigned int boost_time, unsigned int low_power_time) : 
		index(device_index), en_de_coder_enabled(coder_enabled),
		boost_utilization(boost_util), low_power_utilization(low_power_util), 
		boost_activate_time(boost_time), low_power_activate_time(low_power_time) {

		nvmlReturn_t result;
		// Get device handle
		result = nvmlDeviceGetHandleByIndex(index, &device);
		if(result != NVML_SUCCESS) {
			log_printf(LOG_ERROR, "GPU%d: Failed to get device handle: %s", index, nvmlErrorString(result));
			supported = false;
			return;
		}

		// Get device name
		char device_name[NVML_DEVICE_NAME_BUFFER_SIZE];
		result = nvmlDeviceGetName(device, device_name, NVML_DEVICE_NAME_BUFFER_SIZE);
		if(result != NVML_SUCCESS) {
			log_printf(LOG_ERROR, "GPU%d: Failed to get device name: %s", index, nvmlErrorString(result));
			supported = false;
			return;
		}

		// Get PCI info
		nvmlPciInfo_t pci_info;
		result = nvmlDeviceGetPciInfo(device, &pci_info);
		if(result != NVML_SUCCESS) {
			log_printf(LOG_ERROR, "GPU%d: Failed to get PCI info: %s", index, nvmlErrorString(result));
			supported = false;
			return;
		}

		log_printf(LOG_INFO, "GPU%d: %s (%s) initializing", index, device_name, pci_info.busIdLegacy);

		// Get the max and min memory clocks
		auto mem_clocks = std::make_unique<unsigned int[]>(10);
		unsigned int elements = 10;

		result = nvmlDeviceGetSupportedMemoryClocks(device, &elements, mem_clocks.get());
		if(result != NVML_SUCCESS) {
			log_printf(LOG_ERROR, "GPU%d: Failed to get supported memory clocks: %s", index, nvmlErrorString(result));
			supported = false;
			return;
		}

		log_printf(LOG_DEBUG, "GPU%d: Supported memory clocks:", index);
		for(unsigned int i = 0; i < elements; i++) {
			log_printf(LOG_DEBUG, "GPU%d: %d MHz", index, mem_clocks[i]);
		}

		// The return value seems to be arranged from max to min
		// Push elements to vector
		for(unsigned int i = 0; i < elements; i++) {
			clocks.push_back(mem_clocks[i]);
		}
		max_power_state = elements - 1;

		log_printf(LOG_DEBUG, "GPU%d: Registered power states: %d", index, elements);

		// Try to set to max power state
		result = nvmlDeviceSetMemoryLockedClocks(device, clocks[0], clocks[0]);
		if(result != NVML_SUCCESS) {
			log_printf(LOG_ERROR, "GPU%d: Failed to manipulate clocks: %s", index, nvmlErrorString(result));
			supported = false;
			return;
		}

		// Reset last update time
		last_update = std::chrono::steady_clock::now();

		log_printf(LOG_DEBUG, "GPU%d: Boost utilization: %d%%", index, boost_utilization);
		log_printf(LOG_DEBUG, "GPU%d: Low power utilization: %d%%", index, low_power_utilization);
		log_printf(LOG_DEBUG, "GPU%d: Boost time: %d ms", index, boost_activate_time);
		log_printf(LOG_DEBUG, "GPU%d: Low power time: %d ms", index, low_power_activate_time);
		log_printf(LOG_DEBUG, "GPU%d: Encoder and decoder utilization: %s", index, coder_enabled ? "enabled" : "disabled");

		log_printf(LOG_INFO, "GPU%d: %s (%s) initialized", index, device_name, pci_info.busIdLegacy);
	};

	~PowermizerInstance() {
		if(supported) {
			nvmlReturn_t result;

			// Reset control
			log_printf(LOG_DEBUG, "GPU%d: Resetting memory clocks", index);
			result = nvmlDeviceResetMemoryLockedClocks(device);
			if(result != NVML_SUCCESS) {
				log_printf(LOG_ERROR, "GPU%d: Failed to reset memory clocks: %s", index, nvmlErrorString(result));
			}
		}
	};

	bool is_supported() {
		return supported;
	}

	void process() {
		nvmlReturn_t result;
		nvmlUtilization_t utilization;
		unsigned int encoder_utilization = 0;
		unsigned int decoder_utilization = 0;
		unsigned int sampling_period;

		// Get current time
		auto now = std::chrono::steady_clock::now();

		// Get current GPU usage
		result = nvmlDeviceGetUtilizationRates(device, &utilization);
		if(result != NVML_SUCCESS) {
			log_printf(LOG_ERROR, "GPU%d: Failed to get utilization: %s", index, nvmlErrorString(result));
			return;
		}
		
		if(en_de_coder_enabled) {
			result = nvmlDeviceGetEncoderUtilization(device, &encoder_utilization, &sampling_period);
			if(result != NVML_SUCCESS) {
				log_printf(LOG_ERROR, "GPU%d: Failed to get encoder utilization: %s", index, nvmlErrorString(result));
				encoder_utilization = 0;
			}

			result = nvmlDeviceGetDecoderUtilization(device, &decoder_utilization, &sampling_period);
			if(result != NVML_SUCCESS) {
				log_printf(LOG_ERROR, "GPU%d: Failed to get decoder utilization: %s", index, nvmlErrorString(result));
				decoder_utilization = 0;
			}
		}

		max_utilization = std::max(utilization.gpu, std::max(encoder_utilization, decoder_utilization));

		// Check if we need to change power state
		// Boost condition
		if(power_state > 0) {
			if(max_utilization >= boost_utilization) {
				// Check if time exceeded
				auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update);
				if(duration.count() >= boost_activate_time) {
					int new_clock = clocks[power_state - 1];
					log_printf(LOG_DEBUG, "GPU%d: Boosting clock to %d", index, new_clock);
					result = nvmlDeviceSetMemoryLockedClocks(device, new_clock, new_clock);
					if(result != NVML_SUCCESS) {
						log_printf(LOG_ERROR, "GPU%d: Failed to reset memory clocks: %s", index, nvmlErrorString(result));
						return;
					}
					// Update update time
					last_update = now;
					power_state--;
				}
				// Action has pended or taken, stop processing
				return;
			}
		}

		// Low power condition
		if(power_state < max_power_state) {
			if(max_utilization <= low_power_utilization) {
				// Check if time exceeded
				auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update);
				if(duration.count() >= low_power_activate_time) {
					int new_clock = clocks[power_state + 1];
					log_printf(LOG_DEBUG, "GPU%d: Lowering clock to %d", index, new_clock);
					result = nvmlDeviceSetMemoryLockedClocks(device, new_clock, new_clock);
					if(result != NVML_SUCCESS) {
						log_printf(LOG_ERROR, "GPU%d: Failed to set memory clocks: %s", index, nvmlErrorString(result));
						return;
					}
					// Update update time
					last_update = now;
					power_state++;
				}
				// Action has pended or taken, stop processing
				return;
			}
		}

		// No action has taken
		// Update last update time
		last_update = now;
	}

private:
	// Target device handle
	nvmlDevice_t device;
	int index;

	// Config vars
	bool en_de_coder_enabled;
	unsigned int boost_utilization;
	unsigned int low_power_utilization;
	unsigned int boost_activate_time;
	unsigned int low_power_activate_time;
	std::vector<int> clocks = {};
	bool supported = true;

	// Process vars
	int power_state = 0;
	int max_power_state = 0;
	unsigned int max_utilization;
	std::chrono::steady_clock::time_point last_update;
};

void print_usage(const char *progname) {
	printf("Usage: %s [options]\n", progname);
	printf("Options:\n");
	printf("  -h, --help                   Print this help message\n");
	printf("  -b, --boost <util>           Set the utilization threshold to boost power state\n");
	printf("  -l, --low-power <util>       Set the utilization threshold to lower power state\n");
	printf("  -B, --boost-time <ms>        Set the time to boost power state\n");
	printf("  -L, --low-power-time <ms>    Set the time to lower power state\n");
	printf("  -c, --coder                  Enable encoder and decoder utilization\n");
	printf("  -v, --verbose                Increase verbosity\n");
}

static bool running = true;

void stopsig_handler(int sig) {
	(void)sig;
	running = false;
}

int main(int argc, char *argv[]) {
	int verbose = 0;
	int boost_util = -1;
	int low_power_util = -1;
	int boost_time = -1;
	int low_power_time = -1;
	bool coder_enabled = false;

	int c;
	int option_index = 0;
	static struct option long_options[] = {
		{"help",            no_argument,        0, 'h'},
		{"boost",           required_argument,  0, 'b'},
		{"low-power",       required_argument,  0, 'l'},
		{"boost-time",      required_argument,  0, 'B'},
		{"low-power-time",  required_argument,  0, 'L'},
		{"coder",           no_argument,        0, 'c'},
		{"verbose",         no_argument,        0, 'v'},
		{0, 0, 0, 0}
	};

	while((c = getopt_long(argc, argv, "hb:l:B:L:cv", long_options, &option_index)) != -1) {
		switch(c) {
			case 'h':
				print_usage(argv[0]);
				return 0;
			case 'b':
				boost_util = atoi(optarg);
				break;
			case 'l':
				low_power_util = atoi(optarg);
				break;
			case 'B':
				boost_time = atoi(optarg);
				break;
			case 'L':
				low_power_time = atoi(optarg);
				break;
			case 'c':
				coder_enabled = true;
				break;
			case 'v':
				verbose++;
				break;
			default:
				print_usage(argv[0]);
				return 1;
		}
	}
	
	if(boost_util == -1) {
		printf("Error: Boost utilization threshold is not set\n");
		print_usage(argv[0]);
		return 1;
	}
	if(low_power_util == -1) {
		printf("Error: Low power utilization threshold is not set\n");
		print_usage(argv[0]);
		return 1;
	}
	if(boost_time == -1) {
		printf("Error: Boost time is not set\n");
		print_usage(argv[0]);
		return 1;
	}
	if(low_power_time == -1) {
		printf("Error: Low power time is not set\n");
		print_usage(argv[0]);
		return 1;
	}

	if(verbose > 0) {
		current_loglevel = LOG_DEBUG;
	}

	log_printf(LOG_INFO, "NVIDIA Powermizer " VERSION " starting");

	// Initialize NVML
	log_printf(LOG_DEBUG, "Initializing NVML");
	nvmlReturn_t result;
	result = nvmlInit();
	if(result != NVML_SUCCESS) {
		log_printf(LOG_FATAL, "Failed to initialize NVML: %s", nvmlErrorString(result));
		return 1;
	}

	char nvml_version_str[NVML_SYSTEM_NVML_VERSION_BUFFER_SIZE];
	result = nvmlSystemGetNVMLVersion(nvml_version_str, NVML_SYSTEM_NVML_VERSION_BUFFER_SIZE);
	if(result != NVML_SUCCESS) {
		log_printf(LOG_FATAL, "Failed to get NVML version: %s", nvmlErrorString(result));
		return 1;
	}
	log_printf(LOG_INFO, "NVML version: %s", nvml_version_str);

	char driver_version_str[NVML_DEVICE_NAME_BUFFER_SIZE];
	result = nvmlSystemGetDriverVersion(driver_version_str, NVML_SYSTEM_DRIVER_VERSION_BUFFER_SIZE);
	if(result != NVML_SUCCESS) {
		log_printf(LOG_FATAL, "Failed to get driver version: %s", nvmlErrorString(result));
		return 1;
	}
	log_printf(LOG_INFO, "Driver version: %s", driver_version_str);

	// Get device count
	unsigned int device_count;
	result = nvmlDeviceGetCount(&device_count);
	if(result != NVML_SUCCESS) {
		log_printf(LOG_FATAL, "Failed to get device count: %s", nvmlErrorString(result));
		return 1;
	}

	log_printf(LOG_INFO, "Found %d GPU(s)", device_count);

	log_printf(LOG_INFO, "Initializing GPU(s)");
	std::vector<std::unique_ptr<PowermizerInstance>> instances;
	for(unsigned int i = 0; i < device_count; i++) {
		auto instance = std::make_unique<PowermizerInstance>(i, coder_enabled, boost_util, low_power_util, boost_time, low_power_time);
		if(!instance->is_supported()) {
			log_printf(LOG_WARN, "GPU%d: Not supported", i);
			continue;
		}
		instances.push_back(std::move(instance));
	}
	
	if(instances.size() == 0) {
		log_printf(LOG_FATAL, "No supported GPU found");
		return 1;
	}

	// Determine loop interval
	int loop_interval_us = std::min(boost_time, low_power_time) * 1000;
	log_printf(LOG_DEBUG, "Loop interval: %d us", loop_interval_us);

	// Set signal handler
	log_printf(LOG_DEBUG, "Setting signal handler");
	signal(SIGINT, stopsig_handler);
	signal(SIGTERM, stopsig_handler);

	log_printf(LOG_INFO, "Powermizer started");

	// Main loop
	while(running) {
		for(auto &instance : instances) {
			instance->process();
		}
		usleep(loop_interval_us);
	}

	log_printf(LOG_INFO, "Exiting");

	for(auto &instance : instances) {
		instance.reset();
	}

	// Shutdown NVML
	log_printf(LOG_DEBUG, "Shutting down NVML");

	result = nvmlShutdown();
	if(result != NVML_SUCCESS) {
		log_printf(LOG_ERROR, "Failed to shutdown NVML: %s", nvmlErrorString(result));
		return 1;
	}
	
	return 0;
}