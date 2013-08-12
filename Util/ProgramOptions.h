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

#include <boost/program_options.hpp>

class inut_file_path : public boost::filesystem::path {
public:
    inut_file_path() {}
    inut_file_path(const std::string& path) : boost::filesystem::path(path) {}
};

namespace boost {
    namespace program_options {
        class missing_file_error : public boost::program_options::invalid_option_value {
        public:
            missing_file_error(const std::string& path) : invalid_option_value(path), path(path) {};
            ~missing_file_error() throw() {};
            const char* what() const throw() { 
                std::string err = get_option_name() + " file \"" + path + "\" not found";
                return err.c_str();
            };
        protected:
            std::string path;
        };
    }
}

void validate(boost::any& v, 
              const std::vector<std::string>& values,
              inut_file_path*, int)
{
    using namespace boost::program_options;
    validators::check_first_occurrence(v);      // avoid previous assignments
    const std::string& input_string = validators::get_single_string(values); // avoid multiple assignments
    if(boost::filesystem::is_regular_file(input_string)) {
        v = boost::any(inut_file_path(input_string));
    } else {
        throw missing_file_error(input_string);
    }        
}