#pragma once

#include <boost/log/trivial.hpp>

#define LOG_DBG BOOST_LOG_TRIVIAL(debug)
#define LOG_INF BOOST_LOG_TRIVIAL(info)
#define LOG_WRN BOOST_LOG_TRIVIAL(warning)
#define LOG_ERR BOOST_LOG_TRIVIAL(error)
