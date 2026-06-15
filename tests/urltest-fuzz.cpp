#define UNIT_TEST 1

#include "../src/pcm-sensor-server.cpp"

#undef UNIT_TEST


extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
       try
       {
               std::string buf(reinterpret_cast<const char*>(data), size);
               buf.push_back('\0');
               URL x = URL::parse(buf.c_str());
       }
       catch (std::runtime_error & )
       {
               // catch recognized malformed input (thrown as runtime_error in the URL::parse)
               // do not catch any other errors or exceptions to let them be reported
               // by libFuzzer
       }

       return 0;
}

