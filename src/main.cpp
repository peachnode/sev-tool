/**************************************************************************
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 **************************************************************************/

#include "commands.h"  // has measurement_t
#include "tests.h"     // for test_all
#include "utilities.h" // for str_to_array
#include <getopt.h>    // for getopt_long
#include <stdio.h>
#include <string>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <iostream>

const char help_array[] = "The following commands are supported:\n"
                          " sevtool -[global opts] --[command] [command opts]\n"
                          "(Please see the readme file for more detailed information)\n"
                          "Platform Owner commands:\n"
                          "  factory_reset\n"
                          "  platform_status\n"
                          "  pek_gen\n"
                          "  pek_csr\n"
                          "  pdh_gen\n"
                          "  pdh_cert_export\n"
                          "  pek_cert_import\n"
                          "      Input params:\n"
                          "          pek_csr.signed.cert file\n"
                          "          oca.cert file\n"
                          "  get_id\n"
                          "  sign_pek_csr\n"
                          "      Input params:\n"
                          "          pek_csr.cert file\n"
                          "          [oca private key].pem file\n"
                          "  set_self_owned\n"
                          "  set_externally_owned\n"
                          "      Input params:\n"
                          "          [oca private key].pem file\n"
                          "  generate_cek_ask\n"
                          "  get_ask_ark\n"
                          "  export_cert_chain\n"
                          "Guest Owner commands:\n"
                          "  calc_measurement\n"
                          "      Input params (all in ascii-encoded hex bytes):\n"
                          "          uint8_t  meas_ctx\n"
                          "          uint8_t  api_major\n"
                          "          uint8_t  api_minor\n"
                          "          uint8_t  build_id\n"
                          "          uint32_t policy\n"
                          "          uint32_t digest\n"
                          "          uint8_t  m_nonce[128/8]\n"
                          "          uint8_t  gctx_tik[128/8]\n"
                          "  validate_cert_chain\n"
                          "  generate_launch_blob\n"
                          "      Input params:\n"
                          "          uint32_t policy\n"
                          "  package_secret\n"
                          "  validate_attestation\n"
                          "  validate_guest_report\n"
                          "  validate_cert_chain_vcek\n"
                          "  export_cert_chain_vcek\n";

/* Flag set by '--verbose' */
static int verbose_flag = 0;
static int repetitions = 1; 

static struct option long_options[] =
    {
        /* These options set a flag. */
        {"verbose", no_argument, &verbose_flag, 1},
        {"brief", no_argument, &verbose_flag, 0},

        /* These options don't set a flag. We distinguish them by their indices. */
        /* Platform Owner commands */
        {"factory_reset", no_argument, 0, 'a'},
        {"platform_status", no_argument, 0, 'b'},
        {"pek_gen", no_argument, 0, 'c'},
        {"pek_csr", no_argument, 0, 'd'},
        {"pdh_gen", no_argument, 0, 'e'},
        {"pdh_cert_export", no_argument, 0, 'f'},
        {"pek_cert_import", required_argument, 0, 'g'},
        {"get_id", no_argument, 0, 'j'},
        {"set_self_owned", no_argument, 0, 'k'},
        {"set_externally_owned", required_argument, 0, 'l'},
        {"generate_cek_ask", no_argument, 0, 'm'},
        {"export_cert_chain", no_argument, 0, 'p'},
        {"export_cert_chain_vcek", no_argument, 0, 'q'},
        {"repetitions", required_argument, 0, 'r'},
        {"sign_pek_csr", required_argument, 0, 's'},
        /* Guest Owner commands */
        {"get_ask_ark", no_argument, 0, 'n'},
        {"calc_measurement", required_argument, 0, 't'},
        {"validate_cert_chain", no_argument, 0, 'u'},
        {"generate_launch_blob", required_argument, 0, 'v'},
        {"package_secret", no_argument, 0, 'w'},
        {"validate_attestation", no_argument, 0, 'x'},  // SEV attestation command
        {"validate_guest_report", no_argument, 0, 'y'}, // SNP GuestRequest ReportRequest
        {"validate_cert_chain_vcek", no_argument, 0, 'z'},

        /* Run tests */
        {"test_all", no_argument, 0, 'T'},

        {"help", no_argument, 0, 'H'},
        {"sys_info", no_argument, 0, 'I'},
        {"ofolder", required_argument, 0, 'O'},
        {0, 0, 0, 0}};

template <typename Func>
int perform_repetitions_and_analysis(Func func, int repetitions = 10)
{
    std::vector<double> measurements;
    measurements.reserve(repetitions);
    int cmd_ret = 0;

    for (int i = 0; i < repetitions; i++)
    {
        cmd_ret = func(measurements);

        if (cmd_ret != 0)
        {
            if (cmd_ret == 0xFFFF)
            {
                printf("\nCommand not supported/recognized. Possibly bad formatting\n");
            }
            else
            {
                printf("\nCommand Unsuccessful: 0x%02x\n", cmd_ret);
            }
            break;
        }
    }

    // Compute stats
    
    double sum = std::accumulate(measurements.begin(), measurements.end(), 0.0);
    double mean = sum / static_cast<double>(measurements.size());

    double variance = 0.0;
    for (auto val : measurements)
    {
        variance += (val - mean) * (val - mean);
    }
    variance /= static_cast<double>(measurements.size());

    double stdev = std::sqrt(variance);

    std::sort(measurements.begin(), measurements.end());
    auto minmax = std::minmax_element(measurements.begin(), measurements.end());

    // Calculating the quartiles
    double Q1, Q3;

    int n = measurements.size();

    if (n % 2 == 0)
    {
        Q1 = (measurements[n / 4 - 1] + measurements[n / 4]) / 2;
        Q3 = (measurements[3 * n / 4 - 1] + measurements[3 * n / 4]) / 2;
    }
    else
    {
        Q1 = measurements[n / 4];
        Q3 = measurements[3 * n / 4];
    }
    double median;
    if (measurements.size() % 2 == 0)
        median = (measurements[measurements.size() / 2 - 1] + measurements[measurements.size() / 2]) / 2;
    else
        median = measurements[measurements.size() / 2];
    std::cout << "Min: " << *minmax.first << std::endl;
    std::cout << "First Quartile (Q1): " << Q1 << std::endl;
    std::cout << "Median: " << median << std::endl;
    std::cout << "Third Quartile (Q3): " << Q3 << std::endl;
    std::cout << "Max: " << *minmax.second << std::endl;
    std::cout << "Number of measurements: " << measurements.size() << std::endl;
    std::cout << "Average: " << mean << std::endl;
    std::cout << "Standard Deviation: " << stdev << std::endl;
    std::cout << "Variance: " << variance << std::endl;
    return cmd_ret;
}
int main(int argc, char **argv)
{
    int c = 0;
    int option_index = 0; /* getopt_long stores the option index here. */
    std::string output_folder = "./";

    int cmd_ret = 0xFFFF;

    while ((c = getopt_long(argc, argv, "hio:r:", long_options, &option_index)) != -1)
    {

        switch (c)
        {
        case 'h': // help
        case 'H':
        {
            printf("%s\n", help_array);
            cmd_ret = 0;
            break;
        }
        case 'i': // sys_info
        case 'I':
        {
            Command cmd(output_folder, verbose_flag);
            cmd_ret = cmd.sys_info(); // Display system info
            break;
        }
        case 'o': // ofolder
        case 'O':
        {
            output_folder = optarg;
            output_folder += "/";

            // Check that output folder exists, and immediately stop if not
            std::string cmd = "if test -d " + output_folder + " ; then echo \"exist\"; else echo \"no\"; fi";
            std::string output = "";
            if (!sev::execute_system_command(cmd, &output))
            {
                printf("Error. Output directory %s existance check failed.\n", output_folder.c_str());
                return false;
            }

            if (strncmp(output.c_str(), "exists", 2) != 0)
            {
                printf("Error. Output directory %s does not exist. "
                       "Please manually create it and try again\n",
                       output_folder.c_str());
                return false;
            }

            break;
        }
        case 'a':
        {
            cmd_ret = perform_repetitions_and_analysis([&](std::vector<double> &measurements)
                                                       {
                Command cmd(output_folder, verbose_flag);
                return cmd.factory_reset(measurements); }, repetitions);
            break;
        }
        case 'b':
        { // PLATFORM_STATUS
            cmd_ret = perform_repetitions_and_analysis([&](std::vector<double> &measurements)
                                                       {
                Command cmd(output_folder, verbose_flag);
                return cmd.platform_status(measurements); }, repetitions);
            break;
        }
        case 'c': 
        { // PEK_GEN
            cmd_ret = perform_repetitions_and_analysis([&](std::vector<double> &measurements)
                                                       {
                Command cmd(output_folder, verbose_flag);
                return cmd.pek_gen(measurements); }, repetitions);
            break;
        }
        case 'd':
        { // PEK_CSR
            cmd_ret = perform_repetitions_and_analysis([&](std::vector<double> &measurements)
                                                       {
                Command cmd(output_folder, verbose_flag);
                return cmd.pek_csr(measurements); }, repetitions);
            break;
        }
        case 'e':
        { // PDH_GEN
            cmd_ret = perform_repetitions_and_analysis([&](std::vector<double> &measurements)
                                                       {
                Command cmd(output_folder, verbose_flag);
                return cmd.pdh_gen(measurements); }, repetitions);
            break;
        }
        case 'f':
        { // PDH_CERT_EXPORT
            cmd_ret = perform_repetitions_and_analysis([&](std::vector<double> &measurements)
                                                       {
                Command cmd(output_folder, verbose_flag);
                return cmd.pdh_cert_export(measurements); }, repetitions);
            break;
        }
        case 'g':
        {             // PEK_CERT_IMPORT
            optind--; // Can't use option_index because it doesn't account for '-' flags
            if (argc - optind != 2)
            {
                printf("Error: Expecting exactly 2 args for pek_cert_import\n");
                return false;
            }
            std::string signed_pek_csr_file = argv[optind++];
            std::string oca_cert_file = argv[optind++];
            cmd_ret = perform_repetitions_and_analysis([&](std::vector<double> &measurements)
                                                       {
                Command cmd(output_folder, verbose_flag);
                return cmd.pek_cert_import(signed_pek_csr_file, oca_cert_file); }, repetitions);

            break;
        }
        case 'j':
        { // GET_ID
            cmd_ret = perform_repetitions_and_analysis([&](std::vector<double> &measurements)
                                                       {
                Command cmd(output_folder, verbose_flag);
                return cmd.get_id(measurements); }, repetitions);
            break;
        }
        case 'k':
        { // SET_SELF_OWNED
            Command cmd(output_folder, verbose_flag);
            cmd_ret = cmd.set_self_owned();
            break;
        }
        case 'l':
        {             // SET_EXTERNALLY_OWNED
            optind--; // Can't use option_index because it doesn't account for '-' flags
            if (argc - optind != 1)
            {
                printf("Error: Expecting exactly 1 arg for set_externally_owned\n");
                return false;
            }

            std::string oca_priv_key_file = argv[optind++];
            Command cmd(output_folder, verbose_flag);
            cmd_ret = cmd.set_externally_owned(oca_priv_key_file);
            break;
        }
        case 'm':
        { // GENERATE_CEK_ASK
            Command cmd(output_folder, verbose_flag);
            cmd_ret = cmd.generate_cek_ask();
            break;
        }
        case 'n':
        { // GET_ASK_ARK
            Command cmd(output_folder, verbose_flag, CCP_NOT_REQ);
            cmd_ret = cmd.get_ask_ark();
            break;
        }
        case 'p':
        { // EXPORT_CERT_CHAIN
            Command cmd(output_folder, verbose_flag);
            cmd_ret = cmd.export_cert_chain();
            break;
        }
        case 'q':
        { // EXPORT_CERT_CHAIN_VCEK
            Command cmd(output_folder, verbose_flag);
            cmd_ret = cmd.export_cert_chain_vcek();
            break;
        }
        case 'r':
        {
            repetitions = atoi(optarg);
            if (repetitions <= 0)
            {
                printf("Error: Invalid repetitions value %d. Using default.\n", repetitions);
                repetitions = 1;  // Reset to default
            }
            break;
        }
        case 's':
        { // SIGN_PEK_CSR
            optind--;
            if (argc - optind != 2)
            {
                printf("Error: Expecting exactly 2 args for pek_cert_import\n");
                return false;
            }
            std::string pek_csr_file = argv[optind++];
            std::string oca_priv_key_file = argv[optind++];

            Command cmd(output_folder, verbose_flag, CCP_NOT_REQ);
            cmd_ret = cmd.sign_pek_csr(pek_csr_file, oca_priv_key_file);
            break;
        }
        case 't':
        {             // CALC_MEASUREMENT
            optind--; // Can't use option_index because it doesn't account for '-' flags
            if (argc - optind != 8)
            {
                printf("Error: Expecting exactly 8 args for calc_measurement\n");
                return false;
            }

            measurement_t user_data;
            user_data.meas_ctx = (uint8_t)strtol(argv[optind++], NULL, 16);
            user_data.api_major = (uint8_t)strtol(argv[optind++], NULL, 16);
            user_data.api_minor = (uint8_t)strtol(argv[optind++], NULL, 16);
            user_data.build_id = (uint8_t)strtol(argv[optind++], NULL, 16);
            user_data.policy = (uint32_t)strtol(argv[optind++], NULL, 16);
            sev::str_to_array(std::string(argv[optind++]), (uint8_t *)&user_data.digest, sizeof(user_data.digest));
            sev::str_to_array(std::string(argv[optind++]), (uint8_t *)&user_data.mnonce, sizeof(user_data.mnonce));
            sev::str_to_array(std::string(argv[optind++]), (uint8_t *)&user_data.tik, sizeof(user_data.tik));
            Command cmd(output_folder, verbose_flag, CCP_NOT_REQ);
            cmd_ret = cmd.calc_measurement(&user_data);
            break;
        }
        case 'u':
        { // VALIDATE_CERT_CHAIN
            Command cmd(output_folder, verbose_flag, CCP_NOT_REQ);
            cmd_ret = cmd.validate_cert_chain();
            break;
        }
        case 'v':
        {             // GENERATE_LAUNCH_BLOB
            optind--; // Can't use option_index because it doesn't account for '-' flags
            if (argc - optind != 1)
            {
                printf("Error: Expecting exactly 1 arg for generate_launch_blob\n");
                return false;
            }

            uint32_t guest_policy = (uint8_t)strtol(argv[optind++], NULL, 16);
            Command cmd(output_folder, verbose_flag, CCP_NOT_REQ);
            cmd_ret = cmd.generate_launch_blob(guest_policy);
            break;
        }
        case 'w':
        { // PACKAGE_SECRET
            Command cmd(output_folder, verbose_flag, CCP_NOT_REQ);
            cmd_ret = cmd.package_secret();
            break;
        }
        case 'x':
        { // VALIDATE_ATTESTATION
            Command cmd(output_folder, verbose_flag, CCP_NOT_REQ);
            cmd_ret = cmd.validate_attestation();
            break;
        }
        case 'y':
        { // VALIDATE_GUEST_REPORT
            Command cmd(output_folder, verbose_flag, CCP_NOT_REQ);
            cmd_ret = cmd.validate_guest_report();
            break;
        }
        case 'z':
        { // VALIDATE_CERT_CHAIN_VCEK
            Command cmd(output_folder, verbose_flag, CCP_NOT_REQ);
            cmd_ret = cmd.validate_cert_chain_vcek();
            break;
        }
        case 'T':
        { // Run Tests
            Tests test(output_folder, verbose_flag);
            cmd_ret = (test.test_all() == 0); // 0 = fail, 1 = pass
            break;
        }
        case 0:
        case 1:
        {
            // Verbose/brief
            break;
        }
        default:
        {
            fprintf(stderr, "Unrecognised option: -%c\n", optopt);
            return false;
        }
        }
    }

    if (cmd_ret == 0)
    {
        printf("\nCommand Successful\n");
    }
    else if (cmd_ret == 0xFFFF)
    {
        printf("\nCommand not supported/recognized. Possibly bad formatting\n");
    }
    else
    {
        printf("\nCommand Unsuccessful: 0x%02x\n", cmd_ret);
    }

    return 0;
}
