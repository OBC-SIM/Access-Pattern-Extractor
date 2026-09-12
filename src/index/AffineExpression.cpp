#include "AffineExpression.hpp"

#include <stdexcept>

using namespace llvm;

namespace lat::index
{

[[noreturn]] void rejectAffine(const char * reason)
{
  throw std::invalid_argument(std::string("unsupported affine index: ") +
                              reason);
}

int64_t affineInteger(const APInt & value)
{
  if (!value.isSignedIntN(64)) rejectAffine("integer exceeds int64 range");
  return value.getSExtValue();
}

APInt affineWide(int64_t value)
{
  return APInt(128, static_cast<uint64_t>(value), true);
}

void addAffine(AffineExpression & target, const AffineExpression & source,
               int64_t coefficient)
{
  target.constant =
    affineInteger(affineWide(target.constant) +
                  affineWide(coefficient) * affineWide(source.constant));
  for (const auto & [name, term] : source.terms)
  {
    auto found = target.terms.find(name);
    const auto previous =
      found == target.terms.end() ? 0 : found->second.coefficient;
    const auto combined =
      affineInteger(affineWide(previous) +
                    affineWide(coefficient) * affineWide(term.coefficient));
    if (combined == 0)
      target.terms.erase(name);
    else
    {
      auto replacement = term;
      replacement.coefficient = combined;
      target.terms.insert_or_assign(name, std::move(replacement));
    }
  }
}

std::pair<APInt, APInt> affineRange(const AffineExpression & expression)
{
  auto minimum = affineWide(expression.constant);
  auto maximum = minimum;
  for (const auto & [name, term] : expression.terms)
  {
    const auto coefficient = affineWide(term.coefficient);
    const auto low =
      coefficient * (term.coefficient < 0 ? term.maximum : term.minimum);
    const auto high =
      coefficient * (term.coefficient < 0 ? term.minimum : term.maximum);
    bool lowOverflow, highOverflow;
    minimum = minimum.sadd_ov(low, lowOverflow);
    maximum = maximum.sadd_ov(high, highOverflow);
    if (lowOverflow || highOverflow)
      rejectAffine("affine range exceeds int128");
  }
  return {std::move(minimum), std::move(maximum)};
}

void checkAffineRange(const AffineExpression & expression, unsigned width)
{
  const auto [minimum, maximum] = affineRange(expression);
  if (!minimum.isSignedIntN(width) || !maximum.isSignedIntN(width) ||
      !minimum.isSignedIntN(64) || !maximum.isSignedIntN(64))
    rejectAffine("index may wrap or narrow");
}

std::string formatAffine(const AffineExpression & expression)
{
  std::string result;
  const auto append = [&](int64_t value, const std::string & name)
  {
    const auto magnitude = value < 0 ? 0 - static_cast<uint64_t>(value)
                                     : static_cast<uint64_t>(value);
    if (value < 0)
      result += "-";
    else if (!result.empty())
      result += "+";
    if (name.empty() || magnitude != 1)
      result += std::to_string(magnitude) + (name.empty() ? "" : "*");
    result += name;
  };
  for (const auto & [name, term] : expression.terms)
  {
    if (name.empty() || (name.front() >= '0' && name.front() <= '9') ||
        name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstu"
                               "vwxyz_0123456789") != std::string::npos)
      rejectAffine("variable name is outside the affine grammar");
    append(term.coefficient, name);
  }
  if (expression.constant != 0 || result.empty())
    append(expression.constant, "");
  return result;
}

}  // namespace lat::index
