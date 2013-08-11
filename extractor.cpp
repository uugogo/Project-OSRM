/*
    open source routing machine
    Copyright (C) Dennis Luxen, others 2010

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU AFFERO General Public License as published by
the Free Software Foundation; either version 3 of the License, or
any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
or see http://www.gnu.org/licenses/agpl.txt.
 */

#include "Extractor/ExtractorCallbacks.h"
#include "Extractor/ExtractionContainers.h"
#include "Extractor/ScriptingEnvironment.h"
#include "Extractor/PBFParser.h"
#include "Extractor/XMLParser.h"
#include "Util/MachineInfo.h"
#include "Util/OpenMPWrapper.h"
#include "Util/OSRMException.h"
#include "Util/SimpleLogger.h"
#include "Util/StringUtil.h"
#include "Util/UUID.h"
#include "typedefs.h"

#include <cstdlib>

#include <boost/program_options.hpp>

#include <iostream>
#include <fstream>
#include <string>

ExtractorCallbacks * extractCallBacks;
UUID uuid;

int main (int argc, char *argv[]) {
    try {
        LogPolicy::GetInstance().Unmute();
        double startup_time = get_timestamp();

        std::string name_of_binary = boost::filesystem::basename(argv[0]);

        const std::string version_string = "0.3.4";
        const std::string default_profile_path = "profile.lua";
        const int default_num_threads = 10;
        const std::string default_config_path = "extract.cfg";

        std::string input_path;
        std::string profile_path;
        std::string config_file_path;
        int requested_num_threads = default_num_threads;

        // parse options
        try {
            // declare a group of options that will be allowed only on command line
            boost::program_options::options_description generic_options("Options");
            generic_options.add_options()
                ("version,v", "Show version")
                ("help,h", "Show this help message")
                ("config,c", boost::program_options::value<std::string>(&config_file_path)->default_value(default_config_path),
                      "Path to a configuration file.");

            // declare a group of options that will be 
            // allowed both on command line and in config file
            boost::program_options::options_description config_options("Configuration");
            config_options.add_options()
                ("profile,p", boost::program_options::value<std::string>(&profile_path)->default_value(default_profile_path),
                    "Path to LUA routing profile")
                ("threads,t", boost::program_options::value<int>(&requested_num_threads)->default_value(default_num_threads), 
                    "Number of threads to use");

            // hidden options, will be allowed both on command line and in config file,
            // but will not be shown to the user.
            boost::program_options::options_description hidden_options("Hidden options");
            hidden_options.add_options()
                ("input,i", boost::program_options::value<std::string>(&input_path),
                    "Input file in .osm, .osm.bz2 or .osm.pbf format");

            // positional option
            boost::program_options::positional_options_description positional_options;
            positional_options.add("input", 1);

            // combine above options for parsing
            boost::program_options::options_description cmdline_options;
            cmdline_options.add(generic_options).add(config_options).add(hidden_options);

            boost::program_options::options_description config_file_options;
            config_file_options.add(config_options).add(hidden_options);

            boost::program_options::options_description visible_options(name_of_binary + " <input.osm/.osm.bz2/.osm.pbf> [<profile.lua>]");
            visible_options.add(generic_options).add(config_options);

            // parse command line options
            boost::program_options::variables_map option_variables;
            boost::program_options::store(boost::program_options::command_line_parser(argc, argv).
                options(cmdline_options).positional(positional_options).run(), option_variables);

            if(option_variables.count("version")) {
                SimpleLogger().Write() << std::endl << name_of_binary << ", version " << version_string;
                return 0;
            }

            if(option_variables.count("help")) {
                SimpleLogger().Write() << visible_options;
                return 0;
            }

            boost::program_options::notify(option_variables);

            // parse config file
            std::ifstream ifs(config_file_path.c_str());
            if(ifs) {
                SimpleLogger().Write() << "Config file: " << config_file_path;
                boost::program_options::store(parse_config_file(ifs, config_file_options), option_variables);
                boost::program_options::notify(option_variables);
            } else if(!option_variables["config"].defaulted()) {
                // complain if user supplied a config file, but it wasn't found
                SimpleLogger().Write() << "Cannot open config file: " << config_file_path;
                return -1;
            }

            if(!option_variables.count("input")) {
                SimpleLogger().Write(logWARNING) << "An input file must be specified.";
                return -1;
            }

            if(requested_num_threads<1) {
                SimpleLogger().Write(logWARNING) << "Number of threads must be 1 or larger.";
                return -1;
            }

            SimpleLogger().Write() << "Input file: " << input_path;
            SimpleLogger().Write() << "Profile: " << profile_path;
            SimpleLogger().Write() << "Threads: " << requested_num_threads;
        } catch(boost::program_options::too_many_positional_options_error& e) {
            SimpleLogger().Write(logWARNING) << "Only one input file can be specified.";
            return -1;
        } catch(boost::program_options::error& e) {
            SimpleLogger().Write(logWARNING) << e.what();
            return -1;
        }

        /*** Setup Scripting Environment ***/
        ScriptingEnvironment scriptingEnvironment(profile_path.c_str());

        omp_set_num_threads( std::min( omp_get_num_procs(), requested_num_threads) );

        bool file_has_pbf_format(false);
        std::string output_file_name(input_path);
        std::string restrictionsFileName(input_path);
        std::string::size_type pos = output_file_name.find(".osm.bz2");
        if(pos==std::string::npos) {
            pos = output_file_name.find(".osm.pbf");
            if(pos!=std::string::npos) {
                file_has_pbf_format = true;
            }
        }
        if(pos==std::string::npos) {
            pos = output_file_name.find(".pbf");
            if(pos!=std::string::npos) {
                file_has_pbf_format = true;
            }
        }
        if(pos!=std::string::npos) {
            output_file_name.replace(pos, 8, ".osrm");
            restrictionsFileName.replace(pos, 8, ".osrm.restrictions");
        } else {
            pos=output_file_name.find(".osm");
            if(pos!=std::string::npos) {
                output_file_name.replace(pos, 5, ".osrm");
                restrictionsFileName.replace(pos, 5, ".osrm.restrictions");
            } else {
                output_file_name.append(".osrm");
                restrictionsFileName.append(".osrm.restrictions");
            }
        }

        unsigned amountOfRAM = 1;
        unsigned installedRAM = GetPhysicalmemory();
        if(installedRAM < 2048264) {
            SimpleLogger().Write(logWARNING) << "Machine has less than 2GB RAM.";
        }

        StringMap stringMap;
        ExtractionContainers externalMemory;

        stringMap[""] = 0;
        extractCallBacks = new ExtractorCallbacks(&externalMemory, &stringMap);
        BaseParser* parser;
        if(file_has_pbf_format) {
            parser = new PBFParser(input_path.c_str(), extractCallBacks, scriptingEnvironment);
        } else {
            parser = new XMLParser(input_path.c_str(), extractCallBacks, scriptingEnvironment);
        }

        if(!parser->ReadHeader()) {
            throw OSRMException("Parser not initialized!");
        }
        SimpleLogger().Write() << "Parsing in progress..";
        double parsing_start_time = get_timestamp();
        parser->Parse();
        SimpleLogger().Write() << "Parsing finished after " <<
            (get_timestamp() - parsing_start_time) <<
            " seconds";

        externalMemory.PrepareData(output_file_name, restrictionsFileName, amountOfRAM);

        delete parser;
        delete extractCallBacks;

        SimpleLogger().Write() <<
            "extraction finished after " << get_timestamp() - startup_time <<
            "s";

         SimpleLogger().Write() << "\nRun:\n./" << name_of_binary <<" " <<
            output_file_name <<
            " " <<
            restrictionsFileName <<
            std::endl;
    } catch(std::exception & e) {
        SimpleLogger().Write(logWARNING) << "Unhandled exception: " << e.what();
        return -1;
    }
    return 0;
}
