#define UNIT_TEST 1

#include "../src/pcm-sensor-server.cpp"

#undef UNIT_TEST

std::vector<std::string> urls{
    "http://otto:test@www.intel.com/~otto/file1.txt",
    "file://localhost/c/mnt/cd/file2.txt",
    "ftp://otto%40yahoo.com:abcd%3B1234@www.intel.com:30/xyz.php?a=1&t=3",
    "gopher://otto@hostname1.intel.com:8080/file3.zyx",
    "www.intel.com",
    "http://www.blah.org/file.html#firstmark",
    "http://www.blah.org/file.html#firstmark%21%23",
    "localhost",
    "https://www.intel.com",
    "://google.com/",
    "https://intc.com/request?",
    "htt:ps//www.intel.com",
    "http://www.intel.com:66666/",
    "http:///",
    "http://[1234::1234::1234/",
    "http://@www.intel.com",
    "http://otto@:www.intel.com",
    "https://:@www.intel.com",
    "https://user:@www.intel.com",
    "http:www.intel.com/",
    "http://ww\x00\x00\x00rstmark\x0a"
};

int main( int, char** ) {
    int errors = 0;
    for ( auto & s : urls ) {
        try {
            std::cout << s << "\n";
            URL x = URL::parse( s );
            x.printURL(std::cout);
	} catch (const std::runtime_error & x ) {
            std::cout << "\"" << s << "\": " << x.what() << "\n";
	    ++errors;
        }
    }
    return errors;
}
