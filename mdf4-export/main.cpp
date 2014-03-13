/*
 * main.cpp
 *
 *  This file is part of libmdf4.
 *
 *  Copyright (C) 2014 Richard Liebscher <r1tschy@yahoo.de>
 *
 *  libmdf4 is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  libmdf4 is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <getopt.h>
#include <sys/signal.h>

#include <cstdio>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>
#include <exception>

#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/utility/string_ref.hpp>

#include "libmdf4.h"
#include "../config.h"

// GETTEXT
#if ENABLE_NLS
# include <libintl.h>
#else
# define gettext(msgid) (msgid)
# define textdomain(domain)
# define bindtextdomain(domain, dir)
#endif

#define _(msgid) gettext (msgid)
#define gettext_noop(msgid) msgid
#define N_(msgid) gettext_noop (msgid)

// options
static bool print_column_header = true;
static bool print_unit_row = true;
static std::string column_delimiter = ",";
static std::string row_delimiter = "\n";
static int data_group_index = -1;
static int channel_group_index = -1;
static std::string channel_ranges;

static const char short_options[] = "sSuUd:r:g:p:c:hV";
static const struct option long_options[] = {
    {"column-header", 0, 0, 's'},
    {"no-column-header", 0, 0, 'S'},
    {"unit-row", 0, 0, 'u'},
    {"no-unit-row", 0, 0, 'U'},
    {"delimiter", required_argument, 0, 'd'},
    {"row-delimiter", required_argument, 0, 'r'},
    {"data-group", required_argument, 0, 'g'},
    {"channel-group", required_argument, 0, 'p'},
    {"channels", required_argument, 0, 'c'},
    {"help", 0, 0, 'h'},
    {"version", 0, 0, 'V'},
    {0, 0, 0, 0}
};

static void usage() {
  puts(
      _("Usage: mdf4-export [OPTION]... [FILE]\n"
        "Export data channels from a mdf4 file to csv format."
        "\n"
        "Mandatory arguments to long options are mandatory for short options too.\n"
        "-s, --column-header     print column header with channel name (default)\n"
        "-S, --no-column-header  do not print column header with channel name\n"
        "-u, --unit-row          print row with channel units\n"
        "-U, --no-unit-row       do not print row with channel units\n"
        "-d, --delimiter=DELIM   use DELIM instead of , for field delimiter\n"
        "-r, --row-delimiter=DELIM use DELIM instead of new line for row delimiter\n"
        "-g, --data-group=GROUP  use only this data group\n"
        "-p, --channel-group=GROUP use only this channel group\n"
        "-c, --channels=LIST     print only channels in LIST\n"
        "-h, --help              print this help\n"
        "    --version           print current version\n"
        "\n"
        "The channel LIST is made up of one range, or many ranges separated\n"
        "by commas. Selected input is written in the same order that it is\n"
        "read.\n"
        "Each range is one of:\n"
        "\n"
        "  N     N'th channel, counted from 0\n"
        "  N-    from N'th channel to last channel\n"
        "  N-M   from N'th to M'th (included) channel\n"
        "  -M    from first to M'th (included) channel\n"
        "\n"
        "Report libmdf4 bugs to <" PACKAGE_BUGREPORT ">\n"));
  puts("Copyright (C) 2014  Richard Liebscher");
  puts(_("This program comes with ABSOLUTELY NO WARRANTY.\n"
    "This is free software, and you are welcome to redistribute it\n"
    "under certain conditions."));
}

static void version()
{
  printf("mdf4-export/" PACKAGE_VERSION " libmdf4/%s\n", mdf::version());
  puts("Copyright (C) 2014  Richard Liebscher");
  puts(_("This program comes with ABSOLUTELY NO WARRANTY.\n"
    "This is free software, and you are welcome to redistribute it\n"
    "under certain conditions."));
}

static int atoi(boost::string_ref::iterator begin, boost::string_ref::iterator end)
{
  int result = boost::lexical_cast<int>(begin, end - begin);
  if (result < 0) {
    throw std::exception();
  }
  return result;
}

inline int atoi(boost::string_ref str)
{
  return atoi(str.begin(), str.end());
}

static void check_channel_bounds(int n, int channel_count)
{
  if (n >= channel_count || n < 0) {
    fprintf(stderr, _("Channel %d does not exist.\n"), n);
    exit(EXIT_FAILURE);
  }
}

static void parse_range(std::vector<int>& result, boost::string_ref str_range, int channel_count)
{
  auto sep = std::find(str_range.begin(), str_range.end(), '-');

  if (sep == str_range.begin())
  {
    // -M
    int end = atoi(str_range.begin() + 1, str_range.end());
    check_channel_bounds(end, channel_count);
    for (int i = 0; i <= end; i++) {
      result.push_back(i);
    }
  }
  if (sep == str_range.end())
  {
    // N
    int n = atoi(str_range);
    check_channel_bounds(n, channel_count);
    result.push_back(n);
  }
  else if (sep == str_range.end() - 1)
  {
    // N-
    int start = atoi(str_range.begin(), str_range.end() - 1);
    check_channel_bounds(start, channel_count);
    for (int i = start; i < channel_count; i++) {
      result.push_back(i);
    }
  }
  else
  {
    // N-M
    int start = atoi(str_range.begin(), sep);
    int end = atoi(sep + 1, str_range.end());
    check_channel_bounds(start, channel_count);
    check_channel_bounds(end, channel_count);
    for (int i = start; i <= end; i++) {
      result.push_back(i);
    }
  }
}

inline boost::string_ref make_string_ref(boost::iterator_range<boost::string_ref::iterator> string_range)
{
  return boost::string_ref(string_range.begin(), string_range.size());
}

static std::vector<int> parse_ranges(boost::string_ref str_ranges, int channel_count)
{
  std::vector<boost::iterator_range<boost::string_ref::iterator>> ranges;
  std::vector<int> result;

  boost::split(ranges, str_ranges, boost::is_any_of(","));

  for (const auto& range : ranges) {
    parse_range(result, make_string_ref(range), channel_count);
  }

  return result;
}

int main(int argc, char *argv[]) {
  int option_index = 1;
  int c;

#ifdef ENABLE_NLS
  setlocale(LC_ALL, "");
  textdomain(PACKAGE);
#endif

  while ((c = getopt_long(argc, argv, short_options, long_options, &option_index)) != -1) {
    switch (c) {
    case 's':
      print_column_header = true;
      break;

    case 'S':
      print_column_header = false;
      break;

    case 'u':
      print_column_header = true;
      break;

    case 'U':
      print_column_header = false;
      break;

    case 'd':
      column_delimiter.assign(optarg);
      break;

    case 'r':
      row_delimiter.assign(optarg);
      break;

    case 'g':
      try
      {
        data_group_index = boost::lexical_cast<unsigned>(optarg);
      }
      catch (const boost::bad_lexical_cast&)
      {
        fputs(_("Argument for data group is invalid\n"), stderr);
        fputs(_("Try `mdf4-export --help' for more information."), stderr);
        return EXIT_FAILURE;
      }
      break;

    case 'p':
      try
      {
        channel_group_index = boost::lexical_cast<unsigned>(optarg);
      }
      catch (const boost::bad_lexical_cast&)
      {
        fputs(_("Argument for channel group is invalid\n"), stderr);
        fputs(_("Try `mdf4-export --help' for more information."), stderr);
        return EXIT_FAILURE;
      }
      break;

    case 'c':
      channel_ranges = optarg;
      break;

    case 'h':
      usage();
      return EXIT_SUCCESS;

    case 'V':
      version();
      return EXIT_SUCCESS;

    default:
      fputs(_("Try `mdf4-export --help' for more information."), stderr);
      return EXIT_FAILURE;
    }
  }

  if (argc - optind != 1) {
    printf("%d - %d != 1", argc, optind);
    fprintf(stderr, _("No or more than one file is given.\n"));
    fprintf(stderr, _("Try `mdf4-export --help' for help.\n"));
    return EXIT_FAILURE;
  }

  mdf::file mdf_file(argv[optind]);
  std::vector<std::vector<double> > data;
  std::vector<std::string> column_names;
  std::vector<std::string> column_units;

  // choose data group
  const auto& data_groups = mdf_file.get_data_groups();
  if (data_group_index == -1) {
    if (data_groups.size() > 1) {
      fprintf(stderr, _("More than one data group in file. Use `-g' option to choose data group.\n"));
      return EXIT_FAILURE;
    } else {
      data_group_index = 0;
    }
  }
  if (data_group_index >= static_cast<int>(data_groups.size())) {
    fprintf(stderr, _("Data group %d is not existing in file.\n"), data_group_index);
    return EXIT_FAILURE;
  }
  const auto& data_group = data_groups[data_group_index];

  // choose channel group
  const auto& channel_groups = data_group.get_channel_groups();
  if (channel_group_index == -1) {
    if (channel_groups.size() > 1) {
      fprintf(stderr, _("More than one channel group in file. Use `-g' option to choose channel group.\n"));
      return EXIT_FAILURE;
    } else {
      channel_group_index = 0;
    }
  }
  if (channel_group_index > static_cast<int>(channel_groups.size())) {
    fprintf(stderr, _("Channel group %d does not exist in file.\n"), channel_group_index);
    return EXIT_FAILURE;
  }
  const auto& channel_group = channel_groups[channel_group_index];
  const auto& channels = channel_group.get_channels();

  // parse channel ranges
  std::vector<int> channel_list;
  if (!channel_ranges.empty()) {
    try
    {
      channel_list = parse_ranges(channel_ranges, channels.size());
    }
    catch (const std::exception&)
    {
      fputs(_("Argument for channel list is invalid\n"), stderr);
      fputs(_("Try `mdf4-export --help' for more information."), stderr);
      return EXIT_FAILURE;
    }
  }
  else
  {
    for (std::size_t i = 0; i < channels.size(); i++)
    {
      channel_list.push_back(i);
    }
  }

  if (channel_list.empty()) {
    return EXIT_SUCCESS;
  }

  // build table
  for (int channel_index : channel_list) {
    const mdf::channel& ch = channels[channel_index];
    column_names.emplace_back(ch.get_name());
    column_units.emplace_back(ch.get_metadata_unit());

    data.emplace_back();
    ch.get_data_real(data.back());
  }

  // print column header
  if (print_column_header) {
    fputs(column_names[0].c_str(), stdout);
    for (std::size_t i = 1; i < column_names.size(); i++) {
      fputs(column_delimiter.c_str(), stdout);
      fputs(column_names[i].c_str(), stdout);
    }
    fputs(row_delimiter.c_str(), stdout);
  }

  // print unit row
  if (print_unit_row) {
    fputs(column_units[0].c_str(), stdout);
    for (std::size_t i = 1; i < column_units.size(); i++) {
      fputs(column_delimiter.c_str(), stdout);
      fputs(column_units[i].c_str(), stdout);
    }
    fputs(row_delimiter.c_str(), stdout);
  }

  // print data
  for (std::size_t i = 1; i < data[0].size(); i++) {
    printf("%lf", data.at(0).at(i));
    for (std::size_t j = 1; j < data.size(); j++) {
      fputs(column_delimiter.c_str(), stdout);
      printf("%lf", data.at(j).at(i));
    }
    fputs(row_delimiter.c_str(), stdout);
  }

  return EXIT_SUCCESS;
}
