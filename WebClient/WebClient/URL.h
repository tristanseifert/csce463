#ifndef URL_H
#define URL_H

#include <string>
#include <regex>
#include <unordered_map>

/**
 * An URL
 * 
 * @brief Represents the basic components of an URL in an easier to access.
 *		  Some helpers are provided for making working with URLs easier.
*/
class URL {
	public:
		URL() {};
        URL(const std::string str);
        URL(const char* str) : URL(std::string(str)) { }

		virtual ~URL();

	public:
        std::string getScheme() const
        {
            return this->scheme;
        }
		std::string getUsername() const
		{
            return this->username;
		}
		std::string getPassword() const
		{
            return this->password;
		}
		unsigned int getPort() const
		{
            return this->port;
		}
		std::string getHostname() const
		{
            return this->hostname;
		}
		std::string getPath() const
		{
            return this->path;
		}
		std::string getQuery() const
		{
            return this->query;
		}
		std::string getFragment() const
		{
            return this->fragment;
		}

		static void resolve(const std::string host, struct sockaddr_in* outAddr);

		/// Resolve the hostname of the URL
        void resolve(struct sockaddr_in* outAddr);

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