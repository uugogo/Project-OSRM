/*
    open source routing machine
    Copyright (C) Dennis Luxen, 2010

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


#include "../Library/OSRM.h"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <iostream>
#include <stack>
#include <string>
#include <sstream>

//Dude, real recursions on the OS stack? You must be brave...
void print_tree(boost::property_tree::ptree const& pt, const unsigned recursion_depth)
{
    boost::property_tree::ptree::const_iterator end = pt.end();
    for (boost::property_tree::ptree::const_iterator it = pt.begin(); it != end; ++it) {
        for(unsigned i = 0; i < recursion_depth; ++i) {
            std::cout << " " << std::flush;
        }
        std::cout << it->first << ": " << it->second.get_value<std::string>() << std::endl;
        print_tree(it->second, recursion_depth+1);
    }
}


int main (int argc, char * argv[]) {
    std::cout   << "\n starting up engines, compile at "
                << __DATE__ << ", " __TIME__ << std::endl;
    BaseConfiguration serverConfig((argc > 1 ? argv[1] : "server.ini"));
    OSRM routing_machine((argc > 1 ? argv[1] : "server.ini"));

    RouteParameters route_parameters;
    route_parameters.zoomLevel = 18; //no generalization
    route_parameters.printInstructions = true; //turn by turn instructions
    route_parameters.alternateRoute = true; //get an alternate route, too
    route_parameters.geometry = true; //retrieve geometry of route
    route_parameters.compression = true; //polyline encoding
    route_parameters.checkSum = UINT_MAX; //see wiki
    route_parameters.service = "viaroute"; //that's routing
    route_parameters.outputFormat = "json";
    route_parameters.jsonpParameter = ""; //set for jsonp wrapping
    route_parameters.language = ""; //unused atm
    //route_parameters.hints.push_back(); // see wiki, saves I/O if done properly

    _Coordinate start_coordinate(52.519930*100000,13.438640*100000);
    _Coordinate target_coordinate(52.513191*100000,13.415852*100000);
    route_parameters.coordinates.push_back(start_coordinate);
    route_parameters.coordinates.push_back(target_coordinate);

    http::Reply osrm_reply;

    routing_machine.RunQuery(route_parameters, osrm_reply);

    std::cout << osrm_reply.content << std::endl;

    //attention: super-inefficient hack below:

    std::stringstream ss;
    ss << osrm_reply.content;

    boost::property_tree::ptree pt;
    boost::property_tree::read_json(ss, pt);

    print_tree(pt, 0);
    return 0;
}
