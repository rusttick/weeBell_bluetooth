// `eqp`: microphone and earpiece equalizer profiles (see eq.h / eq.c and doc/initial_design.md, "Equalizer profiles").
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_console.h"
#include "cmds.h"
#include "eq.h"

static const char *kind_name(eq_kind_t k)
{
    switch (k) {
    case EQK_HP: return "high-pass";
    case EQK_LP: return "low-pass";
    case EQK_PEAK: return "peak";
    case EQK_LSHELF: return "low shelf";
    default: return "high shelf";
    }
}

static void print_profile(bool mic, int i)
{
    const eq_profile_t *p = mic ? &eq_mic_profiles[i] : &eq_spk_profiles[i];
    printf("%s profile %d: %s: %s\n", mic ? "microphone" : "earpiece", i, p->name, p->what);
    if (i == 0) {
        return;
    }
    printf("  pre-gain %+.1f dB;", p->pre_db);
    for (int k = 0; k < p->n; k++) {
        printf(" %s %.0f Hz", kind_name(p->s[k].kind), p->s[k].f);
        if (p->s[k].kind == EQK_PEAK || p->s[k].kind == EQK_LSHELF || p->s[k].kind == EQK_HSHELF) {
            printf(" %+.0f dB", p->s[k].db);
        }
        printf("%s", k + 1 < p->n ? "," : "");
    }
    printf("\n  response (dB): ");
    static const int freqs[] = {100, 200, 300, 500, 1000, 2000, 3000, 4000, 5000, 6000, 7000};
    for (size_t k = 0; k < sizeof(freqs) / sizeof(freqs[0]); k++) {
        printf("%d Hz %+.1f%s", freqs[k], eq_response_db(p, (float)freqs[k]), k + 1 < sizeof(freqs) / sizeof(freqs[0]) ? ", " : "\n");
    }
}

static int cmd_eqp(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "list")) {
        for (int m = 1; m >= 0; m--) {
            printf("%s profiles (digit 0 is flat):\n", m ? "MICROPHONE" : "EARPIECE");
            const eq_profile_t *t = m ? eq_mic_profiles : eq_spk_profiles;
            for (int i = 0; i < EQ_PROFILES; i++) {
                printf("  %d  %-20s %s\n", i, t[i].name, t[i].what);
            }
        }
        return 0;
    }
    if (argc >= 3 && (!strcmp(argv[1], "mic") || !strcmp(argv[1], "spk"))) {
        bool mic = !strcmp(argv[1], "mic");
        int idx = !strcmp(argv[2], "off") ? 0 : atoi(argv[2]);
        if (idx < 0 || idx >= EQ_PROFILES || (strcmp(argv[2], "off") && (argv[2][0] < '0' || argv[2][0] > '9'))) {
            printf("profile must be 0-9 (0 or off = flat); 'eqp list' shows them\n");
            return 1;
        }
        eq_select(mic, idx);
        print_profile(mic, idx);
        printf("%s\n", mic ? "applies to: mic level/avg/snr, rec, and the microphone side of a Bluetooth call"
                           : "applies to: tone, sweep, noise, sd play, and the earpiece side of a Bluetooth call");
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "show")) {                 // show a profile's details without selecting it
        int idx = atoi(argv[3]);
        print_profile(!strcmp(argv[2], "mic"), idx < 0 || idx >= EQ_PROFILES ? 0 : idx);
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "bench")) {
        eq_bench();
        return 0;
    }
    if (argc == 1 || (argc >= 2 && !strcmp(argv[1], "status"))) {
        print_profile(true, eq_selected(true));
        print_profile(false, eq_selected(false));
        return 0;
    }
    printf("usage: eqp list | status | mic <0-9|off> | spk <0-9|off> | show mic|spk <0-9> | bench\n"
           "  mic = the microphone profile, spk = the earpiece profile; both stay set until changed or reset.\n"
           "  (the older 'eq' command is a separate single filter on the tone output only)\n");
    return 1;
}

void register_eq_commands(void)
{
    const esp_console_cmd_t cmd = {
        .command = "eqp",
        .help = "Equalizer profiles: eqp list | status | mic <0-9|off> | spk <0-9|off> | show mic|spk <n> | bench",
        .func = cmd_eqp,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
