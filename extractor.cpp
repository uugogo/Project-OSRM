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
#include "Util/IniFile.h"
#include "Util/InputFileUtil.h"
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

        std::string binName = boost::filesystem::basename(argv[0]);
        std::string versionString = "0.3.4";
        std::string defaultProfilePath = "profile.lua";
        
        std::string inputPath;
        std::string profilePath;

        // parse options
        try {
            // declare a group of options that will be allowed only on command line
            boost::program_options::options_description generic("Options");
            generic.add_options()
                ("version,v", "Show version")
                ("help,h", "Show this help message")
                ("profile,p", boost::program_options::value<std::string>(&profilePath)->default_value(defaultProfilePath), "Path to LUA routing profile")
                ;

            // hidden options, will be allowed both on command line and in config file,
            // but will not be shown to the user.
            boost::program_options::options_description hidden("Hidden options");
            hidden.add_options()
                ("input,i", boost::program_options::value<std::string>(&inputPath)->required(), "Input file in .osm, .osm.bz2 or .osm.pbf format")
                ;

            boost::program_options::options_description cmdline_options;
            cmdline_options.add(generic).add(hidden);

            boost::program_options::options_description visible(binName + " <input.osm/.osm.bz2/.osm.pbf> [<profile.lua>]");
            visible.add(generic);

            boost::program_options::positional_options_description p;
            p.add("input", 1);

            boost::program_options::variables_map vm;
            boost::program_options::store(boost::program_options::command_line_parser(argc, argv).
                options(cmdline_options).positional(p).run(), vm);

            if(vm.count("version")) {
                SimpleLogger().Write(logINFO) << std::endl << binName << ", version " << versionString;
                return 0;
            }

            if(vm.count("help")) {
                SimpleLogger().Write(logINFO) << visible;
                return 1;
            }

            // verify options, throws exception if problems
            boost::program_options::notify(vm);

            SimpleLogger().Write(logINFO) << "Input file: " << inputPath;
            SimpleLogger().Write(logINFO) << "Profile: " << profilePath;

        } catch(boost::program_options::required_option& e) {
            SimpleLogger().Write(logWARNING) << "An input file must be specified.";
            return -1;
        } catch(boost::program_options::too_many_positional_options_error& e) {
            SimpleLogger().Write(logWARNING) << "Only one input file can be specified.";
            return -1;
        } catch(boost::program_options::error& e) {
            SimpleLogger().Write(logWARNING) << e.what();
            return -1;
        }

        /*** Setup Scripting Environment ***/
        ScriptingEnvironment scriptingEnvironment(profilePath.c_str());

        unsigned number_of_threads = omp_get_num_procs();
        if(testDataFile("extractor.ini")) {
            IniFile extractorConfig("extractor.ini");
            unsigned rawNumber = stringToInt(extractorConfig.GetParameter("Threads"));
            if( rawNumber != 0 && rawNumber <= number_of_threads) {
                number_of_threads = rawNumber;
            }
        }
        omp_set_num_threads(number_of_threads);

        bool file_has_pbf_format(false);
        std::string output_file_name(inputPath);
        std::string restrictionsFileName(inputPath);
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
            parser = new PBFParser(inputPath.c_str(), extractCallBacks, scriptingEnvironment);
        } else {
            parser = new XMLParser(inputPath.c_str(), extractCallBacks, scriptingEnvironment);
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

         SimpleLogger().Write() << "\nRun:\n./" << binName <<" " <<
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
