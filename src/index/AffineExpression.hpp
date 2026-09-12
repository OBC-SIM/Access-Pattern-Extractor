#pragma once

#include <map>
#include <utility>

#include "LoopInduction.hpp"

namespace lat::index
{

/** @brief One proved variable domain and its constant coefficient. */
struct AffineTerm
{
  int64_t coefficient;
  llvm::APInt minimum;
  llvm::APInt maximum;
};

/** @brief Mathematical affine expression with canonical, nonzero terms. */
struct AffineExpression
{
  int64_t constant = 0;
  std::map<std::string, AffineTerm> terms;
};

/**
 * @brief Reject an unproved index without producing a partial LAT.
 * @param reason Specific diagnostic suffix.
 * @return Never returns; throws std::invalid_argument.
 */
[[noreturn]] void rejectAffine(const char * reason);

/**
 * @brief Convert only values representable in the LAT signed integer domain.
 * @param value Borrowed signed APInt.
 * @return Exact int64; throws on overflow.
 */
int64_t affineInteger(const llvm::APInt & value);

/**
 * @brief Embed a signed LAT integer in the checked intermediate domain.
 * @param value Signed int64 value.
 * @return Signed 128-bit integer.
 */
llvm::APInt affineWide(int64_t value);

/**
 * @brief Accumulate a scaled expression without losing coefficients.
 * @param target Borrowed accumulator, discarded by the caller on failure.
 * @param source Borrowed expression, distinct from target.
 * @param coefficient Constant multiplier.
 * @return Nothing; throws if normalized coefficients/constants exceed int64.
 */
void addAffine(AffineExpression & target, const AffineExpression & source,
               int64_t coefficient = 1);

/**
 * @brief Bound an expression over its proved variable domains.
 * @param expression Borrowed normalized expression.
 * @return Minimum/maximum in checked signed 128-bit arithmetic.
 */
std::pair<llvm::APInt, llvm::APInt>
affineRange(const AffineExpression & expression);

/**
 * @brief Require the expression to preserve signed IR and LAT values.
 * @param expression Borrowed normalized expression.
 * @param width Original IR integer width.
 * @return Nothing; throws on possible wrap or narrowing.
 */
void checkAffineRange(const AffineExpression & expression, unsigned width);

/**
 * @brief Emit sorted terms with the constant last and no zero coefficients.
 * @param expression Borrowed normalized expression.
 * @return Canonical restricted LAT affine string.
 */
std::string formatAffine(const AffineExpression & expression);

}  // namespace lat::index
