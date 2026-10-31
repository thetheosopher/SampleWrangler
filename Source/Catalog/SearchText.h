#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sw
{

    /// Splits free-form search text into lowercase word tokens, mirroring the
    /// FTS5 unicode61 tokenizer closely enough for user queries: ASCII letters
    /// and digits are word characters, other ASCII is a separator, and non-ASCII
    /// (UTF-8) bytes are kept as word characters.
    std::vector<std::string> tokenizeSearchText(std::string_view text);

    /// Builds a safe FTS5 MATCH expression where every token must match as a
    /// prefix (implicit AND), e.g. `"kick"* "808"*`. Returns an empty string
    /// when there are no tokens.
    std::string buildFtsPrefixQuery(const std::vector<std::string> &tokens);

    /// In-memory equivalent of the FTS prefix query against one FTS row: every
    /// query token must be a prefix of at least one token in any of the fields.
    /// Only ASCII is case-folded; unlike FTS5 unicode61, non-ASCII letters and
    /// diacritics must match exactly.
    bool searchTokensMatchFields(const std::vector<std::string> &queryTokens,
                                 const std::vector<std::string_view> &fields);

} // namespace sw
