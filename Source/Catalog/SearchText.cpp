#include "SearchText.h"

#include <algorithm>

namespace sw
{

    namespace
    {
        bool isWordByte(unsigned char c) noexcept
        {
            return c >= 0x80 || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        }

        char toLowerAscii(unsigned char c) noexcept
        {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
        }
    }

    std::vector<std::string> tokenizeSearchText(std::string_view text)
    {
        std::vector<std::string> tokens;
        std::string current;

        for (const char ch : text)
        {
            const auto c = static_cast<unsigned char>(ch);
            if (isWordByte(c))
            {
                current.push_back(toLowerAscii(c));
            }
            else if (!current.empty())
            {
                tokens.push_back(std::move(current));
                current.clear();
            }
        }

        if (!current.empty())
            tokens.push_back(std::move(current));

        return tokens;
    }

    std::string buildFtsPrefixQuery(const std::vector<std::string> &tokens)
    {
        std::string expression;
        for (const auto &token : tokens)
        {
            if (token.empty())
                continue;

            if (!expression.empty())
                expression.push_back(' ');

            expression.push_back('"');
            for (const char ch : token)
            {
                if (ch == '"')
                    expression.push_back('"');
                expression.push_back(ch);
            }
            expression += "\"*";
        }

        return expression;
    }

    bool searchTokensMatchFields(const std::vector<std::string> &queryTokens,
                                 const std::vector<std::string_view> &fields)
    {
        if (queryTokens.empty())
            return true;

        std::vector<std::string> fieldTokens;
        for (const auto field : fields)
        {
            auto tokens = tokenizeSearchText(field);
            fieldTokens.insert(fieldTokens.end(),
                               std::make_move_iterator(tokens.begin()),
                               std::make_move_iterator(tokens.end()));
        }

        return std::all_of(queryTokens.begin(), queryTokens.end(), [&fieldTokens](const std::string &queryToken)
                           { return std::any_of(fieldTokens.begin(), fieldTokens.end(), [&queryToken](const std::string &fieldToken)
                                                { return fieldToken.compare(0, queryToken.size(), queryToken) == 0; }); });
    }

} // namespace sw
