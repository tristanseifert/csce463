#ifndef URL_H
#define URL_H

#include <string>
#include <regex>
#include <unordered_map>

/**
 * An URL
 * 
 * @brief Represents the basic components of an URL in an easier to access
*/
class URL {
	public:
		URL() = delete;
        URL(const std::string str);
        URL(const char* str) : URL(std::string(str)) { }

		virtual ~URL();

	public:

	private:
            void parse(const std::string& in);

	private:
		/// Scheme
        std::string scheme;

		/// Username (if specified)
        std::string username;
		/// Password (if specified)
        std::string password;

		/// Port number
        unsigned int port = 0;
		/// Hostname
        std::string hostname;

		/// Path of the resource to access
        std::string path = "/";
		/// Query string
        std::string query;
		/// URL fragment
        std::string fragment;

	private:
        static const std::regex kUrlRegex;
        static const std::regex kHostRegex;
        static const std::unordered_map<std::string, unsigned int> kPortMap;
};

#endif