#pragma once

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>

#define SET_LOG_LEVEL(LVL) do {\
    boost::log::core::get()->set_filter(boost::log::trivial::severity >= boost::log::trivial::LVL); } while (0)

#define LOG_DBG BOOST_LOG_TRIVIAL(debug)
#define LOG_INF BOOST_LOG_TRIVIAL(info)
#define LOG_WRN BOOST_LOG_TRIVIAL(warning)
#define LOG_ERR BOOST_LOG_TRIVIAL(error)
