#include "app_config.hpp"
#include <getopt.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>

AppConfig AppConfig::parse_cli(int argc, char* argv[]) {
    AppConfig config;

    static struct option long_opts[] = {
        {"all",            no_argument,       nullptr, 'a'},
        {"arm",            required_argument, nullptr, 'A'},
        {"samples",        required_argument, nullptr, 'n'},
        {"stress-threads", required_argument, nullptr, 's'},
        {"core-a",         required_argument, nullptr, '1'},
        {"core-b",         required_argument, nullptr, '2'},
        {"channel-bit",    required_argument, nullptr, 'B'},
        {"channel-offset", required_argument, nullptr, 'O'},
        {"channels",       required_argument, nullptr, 'C'},
        {"raw-prefix",     required_argument, nullptr, 'R'},
        {"help",           no_argument,       nullptr, 'h'},
        {nullptr, 0, nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "aA:n:s:1:2:B:O:C:R:h", long_opts, nullptr)) != -1) {
        switch (opt) {
        case 'a': config.do_all = true; break;
        case 'A':
            if (strcmp(optarg, "single_quiet") == 0) config.do_single_quiet = true;
            else if (strcmp(optarg, "dual_quiet") == 0) config.do_hedged_quiet = true;
            else if (strcmp(optarg, "single_stress") == 0) config.do_single_stress = true;
            else if (strcmp(optarg, "dual_stress") == 0) config.do_hedged_stress = true;
            else { usage(argv[0]); std::exit(1); }
            break;
        case 'n': config.n_samples = atoi(optarg); break;
        case 's': config.n_stress = atoi(optarg); break;
        case '1': config.core_a = atoi(optarg); break;
        case '2': config.core_b = atoi(optarg); break;
        case 'B': config.channel_bit = atoi(optarg); config.channel_offset = 1 << config.channel_bit; break;
        case 'O': config.channel_offset = atoi(optarg); break;
        case 'C': config.n_channels = atoi(optarg); break;
        case 'R': config.raw_prefix = optarg; break;
        case 'h': usage(argv[0]); std::exit(0);
        default:  usage(argv[0]); std::exit(1);
        }
    }

    if (config.do_all) {
        config.do_single_quiet = config.do_hedged_quiet = config.do_single_stress = config.do_hedged_stress = true;
    }

    if (!config.do_single_quiet && !config.do_hedged_quiet && !config.do_single_stress && !config.do_hedged_stress) {
        usage(argv[0]);
        std::exit(1);
    }

    if (config.n_stress > MAX_STRESS) config.n_stress = MAX_STRESS;

    return config;
}

void AppConfig::usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options]\n", prog);
    fprintf(stderr, "  --all                 Run all four arms\n");
    fprintf(stderr, "  --arm ARM             Run specific arm:\n");
    fprintf(stderr, "                          single_quiet, dual_quiet,\n");
    fprintf(stderr, "                          single_stress, dual_stress\n");
    fprintf(stderr, "  --samples N           Samples per arm (default: %d)\n", DEFAULT_SAMPLES);
    fprintf(stderr, "  --stress-threads N    Number of stress threads (default: %d)\n", DEFAULT_STRESS);
    fprintf(stderr, "  --channel-bit N       Physical address bit for channel (default: %d)\n", DEFAULT_CHANNEL_BIT);
    fprintf(stderr, "  --channels N          Number of memory channels (default: %d)\n", DEFAULT_NUM_CHANNELS);
    fprintf(stderr, "  --help                Show this help\n");
}