#include <Storages/ObjectStorage/ReadUnitKind.h>
#include <Common/Exception.h>

#include <boost/range/adaptor/map.hpp>

namespace DB
{

namespace ErrorCodes
{
extern const int BAD_ARGUMENTS;
}

IMPLEMENT_SETTING_ENUM(
    ReadUnitKind,
    ErrorCodes::BAD_ARGUMENTS,
    {{"format_file", ReadUnitKind::FormatFile}, {"self_opening", ReadUnitKind::SelfOpening}})
}
