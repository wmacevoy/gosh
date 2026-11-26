/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

/* REQ19/FWD-6, REQ1/CP-7: which words a stream's end takes (applications.h). What each one does
   is the edge's, and is tested from outside (tests/gosh-command.py, tests/connect-outage.py). */

#include <string>
#include <vector>

#include "src/seam/imp/applications.h"
#include "vendor/facts/facts.h"

typedef std::vector<std::string> Words;

FACTS( Words )
{
  FACT_CHECK( Gosh::Seam::Imp::stream_words_valid( Words { "@connect", "localhost", "22" } ), "@connect HOST PORT" );
  FACT_CHECK( Gosh::Seam::Imp::stream_words_valid( Words { "@connect", "::1", "65535" } ), "an IPv6 address, and the last port" );
  FACT_CHECK( !Gosh::Seam::Imp::stream_words_valid( Words { "@connect", "localhost" } ), "no port: not taken" );
  FACT_CHECK( !Gosh::Seam::Imp::stream_words_valid( Words { "@connect", "", "22" } ), "no host: not taken" );
  FACT_CHECK( !Gosh::Seam::Imp::stream_words_valid( Words { "@connect", "h", "22", "more" } ), "a word too many: not taken" );
  FACT_CHECK( !Gosh::Seam::Imp::stream_words_valid( Words { "@connect", "h", "0" } ), "port 0: not taken" );
  FACT_CHECK( !Gosh::Seam::Imp::stream_words_valid( Words { "@connect", "h", "65536" } ), "past 65535: not taken" );
  FACT_CHECK( !Gosh::Seam::Imp::stream_words_valid( Words { "@connect", "h", "ssh" } ), "a service's name: not taken (a number only)" );
  FACT_CHECK( !Gosh::Seam::Imp::stream_words_valid( Words { "@connect", "h", "-1" } ), "a sign: not taken" );
  FACT_CHECK( !Gosh::Seam::Imp::stream_words_valid( Words { "@connect", "h", "000022" } ), "more than five digits: not taken" );

  /* the three there were before -W, unchanged */
  FACT_CHECK( Gosh::Seam::Imp::stream_words_valid( Words { "@stdio" } ) );
  FACT_CHECK( Gosh::Seam::Imp::stream_words_valid( Words { "@sh", "ls -l" } ) );
  FACT_CHECK( !Gosh::Seam::Imp::stream_words_valid( Words { "@sh" } ) );
  FACT_CHECK( !Gosh::Seam::Imp::stream_words_valid( Words { "@nothing" } ) );
  FACT_CHECK( !Gosh::Seam::Imp::stream_words_valid( Words {} ) );
}

FACTS_REGISTER_ALL()
{
  FACTS_REGISTER( Words );
}

FACTS_MAIN
