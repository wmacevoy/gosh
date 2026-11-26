/* Copyright 2026 Warren D. MacEvoy. GPL-3.0-or-later. See github.com/wmacevoy/gosh/LICENSE-GOSH, and LICENSE-MOSH. */

#include "src/command/imp/sshconfig.h"

#include <sstream>

using namespace Gosh::Command;
using namespace Gosh::Command::Imp;

SshConfig Gosh::Command::Imp::read_ssh_config( const std::string& text )
{
  SshConfig c;
  std::istringstream lines( text );
  std::string line;
  while ( std::getline( lines, line ) ) {
    const size_t space = line.find( ' ' );
    if ( space == std::string::npos ) {
      continue;
    }
    const std::string key = line.substr( 0, space );
    const std::string value = line.substr( space + 1 );
    const bool none = value.empty() || value == "none";
    if ( key == "hostname" ) {
      c.hostname = value;
    } else if ( key == "proxyjump" && !none ) {
      c.proxyjump = value;
    } else if ( key == "proxycommand" && !none ) {
      c.proxycommand = value;
    }
  }
  return c;
}

bool Gosh::Command::Imp::runs_gosh( const std::string& proxycommand )
{
  const size_t start = proxycommand.find_first_not_of( " \t" );
  if ( start == std::string::npos ) {
    return false;
  }
  const size_t end = proxycommand.find_first_of( " \t", start );
  std::string first = proxycommand.substr( start, end == std::string::npos ? std::string::npos : end - start );
  const size_t slash = first.rfind( '/' );
  if ( slash != std::string::npos ) {
    first = first.substr( slash + 1 );
  }
  return first == "gosh" || first == "gosh-client" || first == "gocp";
}

std::string Gosh::Command::Imp::remote_ip_for( const std::string& asked, const SshConfig& config )
{
  if ( !asked.empty() ) {
    return asked;
  }
  if ( !config.proxyjump.empty() || ( !config.proxycommand.empty() && !runs_gosh( config.proxycommand ) ) ) {
    return "remote";
  }
  return "proxy";
}

std::string Gosh::Command::Imp::meetup_address( const std::string& host_port, const SshConfig& config )
{
  const size_t colon = host_port.rfind( ':' );
  if ( config.hostname.empty() || colon == std::string::npos ) {
    return host_port;
  }
  const std::string port = host_port.substr( colon + 1 );
  const bool v6 = config.hostname.find( ':' ) != std::string::npos;
  return ( v6 ? "[" + config.hostname + "]" : config.hostname ) + ":" + port;
}
