// SPDX-License-Identifier: GPL-2.0
#include "statstypes.h"
#include "core/dive.h"
#include "core/divemode.h"
#include "core/divesite.h"
#include "core/pref.h"
#include "core/qthelper.h" // for get_depth_unit() et al.
#include "core/subsurface-time.h"
#include <limits>
#include <QLocale>

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
#define SKIP_EMPTY Qt::SkipEmptyParts
#else
#define SKIP_EMPTY QString::SkipEmptyParts
#endif

// Some of our compilers do not support std::size(). Roll our own for now.
template <typename T, size_t N>
static constexpr size_t array_size(const T (&)[N])
{
	return N;
}

static const constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

// Typedefs for year / quarter or month binners
using year_quarter = std::pair<unsigned short, unsigned short>;
using year_month = std::pair<unsigned short, unsigned short>;

// Note: usually I dislike functions defined inside class/struct
// declarations ("Java style"). However, for brevity this is done
// in this rather template-heavy source file more or less consistently.

// Templates to define invalid values for types and test for said values.
// This is used by the binners: returning such a value means "ignore this dive".
template<typename T> T invalid_value();
template<> int invalid_value<int>()
{
	return std::numeric_limits<int>::max();
}
template<> double invalid_value<double>()
{
	return std::numeric_limits<double>::quiet_NaN();
}
template<> QString invalid_value<QString>()
{
	return QString();
}

static bool is_invalid_value(int i)
{
	return i == std::numeric_limits<int>::max();
}

static bool is_invalid_value(double d)
{
	return std::isnan(d);
}

static bool is_invalid_value(const QString &s)
{
	return s.isEmpty();
}

// Currently, we don't support invalid dates - should we?
static bool is_invalid_value(const year_quarter &)
{
	return false;
}

static bool is_invalid_value(const dive_site *d)
{
	return !d;
}

// First, let's define the virtual destructors of our base classes
StatsBin::~StatsBin()
{
}

StatsBinner::~StatsBinner()
{
}

QString StatsBinner::unitSymbol() const
{
	return QString();
}

StatsType::~StatsType()
{
}

QString StatsBinner::name() const
{
	return QStringLiteral("N/A"); // Some dummy string that should never reach the UI
}

QString StatsBinner::formatLowerBound(const StatsBin &bin) const
{
	return QStringLiteral("N/A"); // Some dummy string that should never reach the UI
}

QString StatsBinner::formatUpperBound(const StatsBin &bin) const
{
	return QStringLiteral("N/A"); // Some dummy string that should never reach the UI
}

double StatsBinner::lowerBoundToFloat(const StatsBin &bin) const
{
	return 0.0;
}

double StatsBinner::upperBoundToFloat(const StatsBin &bin) const
{
	return 0.0;
}

// Default implementation for discrete types: there are no bins between discrete bins.
std::vector<StatsBinPtr> StatsBinner::bins_between(const StatsBin &bin1, const StatsBin &bin2) const
{
	return {};
}

QString StatsType::unitSymbol() const
{
	return {};
}

int StatsType::decimals() const
{
	return 0;
}

double StatsType::toFloat(const dive *d) const
{
	return invalid_value<double>();
}

QString StatsType::nameWithUnit() const
{
	QString s = name();
	QString symb = unitSymbol();
	return symb.isEmpty() ? s : QStringLiteral("%1 [%2]").arg(s, symb);
}

QString StatsType::nameWithBinnerUnit(const StatsBinner &binner) const
{
	QString s = name();
	QString symb = binner.unitSymbol();
	return symb.isEmpty() ? s : QStringLiteral("%1 [%2]").arg(s, symb);
}

const StatsBinner *StatsType::getBinner(int idx) const
{
	std::vector<const StatsBinner *> b = binners();
	if (b.empty())
		return nullptr;
	return idx >= 0 && idx < (int)b.size() ? b[idx] : b[0];
}

std::vector<StatsOperation> StatsType::supportedOperations() const
{
	return {};
}

// Attn: The order must correspond to the StatsOperation enum
static const char *operation_names[] = {
	QT_TRANSLATE_NOOP("StatsTranslations", "Median"),
	QT_TRANSLATE_NOOP("StatsTranslations", "Average"),
	QT_TRANSLATE_NOOP("StatsTranslations", "Time-weighted Avg."),
	QT_TRANSLATE_NOOP("StatsTranslations", "Sum")
};

QStringList StatsType::supportedOperationNames() const
{
	std::vector<StatsOperation> ops = supportedOperations();
	QStringList res;
	res.reserve(ops.size());
	for (StatsOperation op: ops)
		res.push_back(operationName(op));
	return res;
}

StatsOperation StatsType::idxToOperation(int idx) const
{
	std::vector<StatsOperation> ops = supportedOperations();
	if (ops.empty()) {
		qWarning("Stats type %s does not support operations", qPrintable(name()));
		return StatsOperation::Median; // oops!
	}
	return idx < 0 || idx >= (int)ops.size() ? ops[0] : ops[idx];
}

QString StatsType::operationName(StatsOperation op)
{
	int idx = (int)op;
	return idx < 0 || idx >= (int)array_size(operation_names) ? QString()
								  : operation_names[(int)op];
}

double StatsType::average(const std::vector<dive *> &dives) const
{
	double sum = 0.0;
	double count = 0.0;
	for (const dive *d: dives) {
		double v = toFloat(d);
		if (is_invalid_value(v))
			continue;
		sum += v;
		count += 1.0;
	}
	return count > 0.0 ? sum / count : 0.0;
}

double StatsType::averageTimeWeighted(const std::vector<dive *> &dives) const
{
	double sum = 0.0;
	double weight_count = 0.0;
	for (const dive *d: dives) {
		double v = toFloat(d);
		if (is_invalid_value(v))
			continue;
		sum += v * d->duration.seconds;
		weight_count += d->duration.seconds;
	}
	return weight_count > 0.0 ? sum / weight_count : 0.0;
}

std::vector<double> StatsType::values(const std::vector<dive *> &dives) const
{
	std::vector<double> vec;
	vec.reserve(dives.size());
	for (const dive *d: dives) {
		double v = toFloat(d);
		if (is_invalid_value(v))
			continue;
		vec.push_back(v);
	}
	std::sort(vec.begin(), vec.end());
	return vec;
}

// Small helper to calculate quartiles - section of intervals of
// two consecutive elements in a vector. It's not strictly correct
// to interpolate linearly. However, on the one hand we don't know
// the actual distribution, on the other hand for a discrete
// distribution the quartiles are ranges. So what should we do?
static double q1(const double *v)
{
	return (3.0*v[0] + v[1]) / 4.0;
}
static double q2(const double *v)
{
	return (v[0] + v[1]) / 2.0;
}
static double q3(const double *v)
{
	return (v[0] + 3.0*v[1]) / 4.0;
}

StatsQuartiles StatsType::quartiles(const std::vector<dive *> &dives) const
{
	return quartiles(values(dives));
}

// This expects the value vector to be sorted!
StatsQuartiles StatsType::quartiles(const std::vector<double> &vec)
{
	size_t s = vec.size();
	if (s == 0)
		return { 0.0, 0.0, 0.0, 0.0, 0.0 };
	switch (s % 4) {
	default:
		// gcc doesn't recognize that we catch all possible values. disappointing.
	case 0:
		return { vec[0], q3(&vec[s/4 - 1]), q2(&vec[s/2 - 1]), q1(&vec[s - s/4 - 1]), vec[s - 1] };
	case 1:
		return { vec[0], vec[s/4], vec[s/2], vec[s - s/4 - 1], vec[s - 1] };
	case 2:
		return { vec[0], q1(&vec[s/4]), q2(&vec[s/2 - 1]), q3(&vec[s - s/4 - 2]), vec[s - 1] };
	case 3:
		return { vec[0], q2(&vec[s/4]), vec[s/2], q2(&vec[s - s/4 - 2]), vec[s - 1] };
	}
}

double StatsType::sum(const std::vector<dive *> &dives) const
{
	double res = 0.0;
	for (const dive *d: dives) {
		double v = toFloat(d);
		if (!is_invalid_value(v))
			res += v;
	}
	return res;
}

double StatsType::applyOperation(const std::vector<dive *> &dives, StatsOperation op) const
{
	switch (op) {
	case StatsOperation::Median:
		return quartiles(dives).q2;
	case StatsOperation::Average:
		return average(dives);
	case StatsOperation::TimeWeightedAverage:
		return averageTimeWeighted(dives);
	case StatsOperation::Sum:
		return sum(dives);
	default: return 0.0;
	}
}

std::vector<std::pair<double,double>> StatsType::scatter(const StatsType &t2, const std::vector<dive *> &dives) const
{
	std::vector<std::pair<double,double>> res;
	res.reserve(dives.size());
	for (const dive *d: dives) {
		double v1 = toFloat(d);
		double v2 = t2.toFloat(d);
		if (is_invalid_value(v1) || is_invalid_value(v2))
			continue;
		res.emplace_back(v1, v2);
	}
	std::sort(res.begin(), res.end());
	return res;
}

// Silly template, which spares us defining type() member functions.
template<StatsType::Type t>
struct StatsTypeTemplate : public StatsType {
	Type type() const override { return t; }
};

// A simple bin that is based on copyable value and can be initialized from
// that value. This template spares us from writing one-line constructors.
template<typename Type>
struct SimpleBin : public StatsBin {
	Type value;
	SimpleBin(const Type &v) : value(v) { }

	// This must not be called for different types. It will crash with an exception.
	bool operator<(StatsBin &b) const {
		return value < dynamic_cast<SimpleBin &>(b).value;
	}

	bool operator==(StatsBin &b) const {
		return value == dynamic_cast<SimpleBin &>(b).value;
	}
};
using IntBin = SimpleBin<int>;
using StringBin = SimpleBin<QString>;

// A general binner template that works on trivial bins that are based
// on a type that is equality and less-than comparable. The derived class
// must possess:
//  - A to_bin_value() function that turns a dive into a value from
//    which the bins can be constructed.
//  - A lowerBoundToFloatBase() function that turns the value form
//    into a double which is understood by the StatsType.
// The bins must possess:
//  - A member variable "value" of the type it is constructed with.
// Note: this uses the curiously recurring template pattern, which I
// dislike, but it is the easiest thing for now.
template<typename Binner, typename Bin>
struct SimpleBinner : public StatsBinner {
public:
	using Type = decltype(Bin::value);
	std::vector<StatsBinDives> bin_dives(const std::vector<dive *> &dives, bool fill_empty) const override;
	std::vector<StatsBinCount> count_dives(const std::vector<dive *> &dives, bool fill_empty) const override;
	const Binner &derived() const {
		return static_cast<const Binner &>(*this);
	}
	const Bin &derived_bin(const StatsBin &bin) const {
		return dynamic_cast<const Bin &>(bin);
	}
};

// Wrapper around std::lower_bound that searches for a value in a
// vector of pairs. Comparison is made with the first element of the pair.
// std::lower_bound does a binary search and this is used to keep a
// vector in ascending order.
template<typename T1, typename T2>
auto pair_lower_bound(std::vector<std::pair<T1, T2>> &v, const T1 &value)
{
	return std::lower_bound(v.begin(), v.end(), value,
	       			[] (const std::pair<T1, T2> &entry, const T1 &value) {
					return entry.first < value;
				});
}

// Add a dive to a vector of (value, dive_list) pairs. If the value doesn't yet
// exist, create a new entry in the vector.
template<typename T>
using ValueDiveListPair = std::pair<T, std::vector<dive *>>;
template<typename T>
void add_dive_to_value_bin(std::vector<ValueDiveListPair<T>> &v, const T &value, dive *d)
{
	// Does that value already exist?
	auto it = pair_lower_bound(v, value);
	if (it != v.end() && it->first == value)
		it->second.push_back(d);	// Bin exists -> add dive!
	else
		v.insert(it, { value, { d }});	// Bin does not exist -> insert at proper location.
}

// Increase count in a vector of (value, count) pairs. If the value doesn't yet
// exist, create a new entry in the vector.
template<typename T>
using ValueCountPair = std::pair<T, int>;
template<typename T>
void increment_count_bin(std::vector<ValueCountPair<T>> &v, const T &value)
{
	// Does that value already exist?
	auto it = pair_lower_bound(v, value);
	if (it != v.end() && it->first == value)
		++it->second;			// Bin exists -> increment count!
	else
		v.insert(it, { value, 1 });	// Bin does not exist -> insert at proper location.
}

template<typename Binner, typename Bin>
std::vector<StatsBinDives> SimpleBinner<Binner, Bin>::bin_dives(const std::vector<dive *> &dives, bool fill_empty) const
{
	// First, collect a value / dives vector and then produce the final vector
	// out of that. I wonder if that is permature optimization?
	using Pair = ValueDiveListPair<Type>;
	std::vector<Pair> value_bins;
	for (dive *d: dives) {
		Type value = derived().to_bin_value(d);
		if (is_invalid_value(value))
			continue;
		add_dive_to_value_bin(value_bins, value, d);
	}

	// Now, turn that into our result array with allocated bin objects.
	std::vector<StatsBinDives> res;
	res.reserve(value_bins.size());
	for (const Pair &pair: value_bins) {
		StatsBinPtr b = std::make_unique<Bin>(pair.first);
		if (fill_empty && !res.empty()) {
			// Add empty bins, if any
			for (StatsBinPtr &bin: bins_between(*res.back().bin, *b))
				res.push_back({ std::move(bin), {}});
		}
		res.push_back({ std::move(b), std::move(pair.second)});
	}
	return res;
}

template<typename Binner, typename Bin>
std::vector<StatsBinCount> SimpleBinner<Binner, Bin>::count_dives(const std::vector<dive *> &dives, bool fill_empty) const
{
	// First, collect a value / counts vector and then produce the final vector
	// out of that. I wonder if that is permature optimization?
	using Pair = std::pair<Type, int>;
	std::vector<Pair> value_bins;
	for (const dive *d: dives) {
		Type value = derived().to_bin_value(d);
		if (is_invalid_value(value))
			continue;
		increment_count_bin(value_bins, value);
	}

	// Now, turn that into our result array with allocated bin objects.
	std::vector<StatsBinCount> res;
	res.reserve(value_bins.size());
	for (const Pair &pair: value_bins) {
		StatsBinPtr b = std::make_unique<Bin>(pair.first);
		if (fill_empty && !res.empty()) {
			// Add empty bins, if any
			for (StatsBinPtr &bin: bins_between(*res.back().bin, *b))
				res.push_back({ std::move(bin), 0});
		}
		res.push_back({ std::move(b), pair.second});
	}
	return res;
}

// A simple binner (see above) that works on continuous (or numeric) types
// and can return bin-ranges. The binner must implement an inc() function
// that turns a bin into the next-higher bin.
template<typename Binner, typename Bin>
struct SimpleContinuousBinner : public SimpleBinner<Binner, Bin>
{
	using SimpleBinner<Binner, Bin>::derived;
	std::vector<StatsBinPtr> bins_between(const StatsBin &bin1, const StatsBin &bin2) const override;

	// By default the value gives the lower bound, so the format is the same
	QString formatLowerBound(const StatsBin &bin) const override {
		return derived().format(bin);
	}

	// For the upper bound, simply go to the next bin
	QString formatUpperBound(const StatsBin &bin) const override {
		Bin b = SimpleBinner<Binner,Bin>::derived_bin(bin);
		derived().inc(b);
		return formatLowerBound(b);
	}

	// Cast to base value type so that the derived class doesn't have to do it
	double lowerBoundToFloat(const StatsBin &bin) const override {
		const Bin &b = SimpleBinner<Binner,Bin>::derived_bin(bin);
		return derived().lowerBoundToFloatBase(b.value);
	}

	// For the upper bound, simply go to the next bin
	double upperBoundToFloat(const StatsBin &bin) const override {
		Bin b = SimpleBinner<Binner,Bin>::derived_bin(bin);
		derived().inc(b);
		return derived().lowerBoundToFloatBase(b.value);
	}
};


// A continuous binner, where the bin is based on an integer value
// and subsequent bins are adjacent integers.
template<typename Binner, typename Bin>
struct IntBinner : public SimpleContinuousBinner<Binner, Bin>
{
	void inc(Bin &bin) const {
		++bin.value;
	}
};

// An integer based binner, where each bin represents an integer
// range with a fixed size.
template<typename Binner, typename Bin>
struct IntRangeBinner : public IntBinner<Binner, Bin> {
	int bin_size;
	IntRangeBinner(int size)
		: bin_size(size)
	{
	}
	QString format(const StatsBin &bin) const override {
		int value = IntBinner<Binner, Bin>::derived_bin(bin).value;
		QLocale loc;
		return StatsTranslations::tr("%1–%2").arg(loc.toString(value * bin_size),
							  loc.toString((value + 1) * bin_size));
	}
	QString formatLowerBound(const StatsBin &bin) const override {
		int value = IntBinner<Binner, Bin>::derived_bin(bin).value;
		return QStringLiteral("%L1").arg(value * bin_size);
	}
	double lowerBoundToFloatBase(int value) const {
		return static_cast<double>(value * bin_size);
	}
};

template<typename Binner, typename Bin>
std::vector<StatsBinPtr> SimpleContinuousBinner<Binner, Bin>::bins_between(const StatsBin &bin1, const StatsBin &bin2) const
{
	const Bin &b1 = SimpleBinner<Binner,Bin>::derived_bin(bin1);
	const Bin &b2 = SimpleBinner<Binner,Bin>::derived_bin(bin2);
	std::vector<StatsBinPtr> res;
	Bin act = b1;
	derived().inc(act);
	while (act.value < b2.value) {
		res.push_back(std::make_unique<Bin>(act));
		derived().inc(act);
	}
	return res;
}

// A binner that works on string-based bins whereby each dive can
// produce multiple strings (e.g. dive buddies). The binner must
// feature a to_string_list() function that produces a vector of
// QStrings and bins that can be constructed from QStrings.
// Other than that, see SimpleBinner.
template<typename Binner, typename Bin>
struct StringBinner : public StatsBinner {
public:
	std::vector<StatsBinDives> bin_dives(const std::vector<dive *> &dives, bool fill_empty) const override;
	std::vector<StatsBinCount> count_dives(const std::vector<dive *> &dives, bool fill_empty) const override;
	const Binner &derived() const {
		return static_cast<const Binner &>(*this);
	}
	QString format(const StatsBin &bin) const override {
		return dynamic_cast<const Bin &>(bin).value;
	}
};

template<typename Binner, typename Bin>
std::vector<StatsBinDives> StringBinner<Binner, Bin>::bin_dives(const std::vector<dive *> &dives, bool) const
{
	// First, collect a value / dives vector and then produce the final vector
	// out of that. I wonder if that is permature optimization?
	using Pair = ValueDiveListPair<QString>;
	std::vector<Pair> value_bins;
	for (dive *d: dives) {
		for (const QString &s: derived().to_string_list(d)) {
			if (is_invalid_value(s))
				continue;
			add_dive_to_value_bin(value_bins, s, d);
		}
	}

	// Now, turn that into our result array with allocated bin objects.
	std::vector<StatsBinDives> res;
	res.reserve(value_bins.size());
	for (const Pair &pair: value_bins)
		res.push_back({ std::make_unique<Bin>(pair.first), std::move(pair.second)});
	return res;
}

template<typename Binner, typename Bin>
std::vector<StatsBinCount> StringBinner<Binner, Bin>::count_dives(const std::vector<dive *> &dives, bool) const
{
	// First, collect a value / counts vector and then produce the final vector
	// out of that. I wonder if that is permature optimization?
	using Pair = std::pair<QString, int>;
	std::vector<Pair> value_bins;
	for (const dive *d: dives) {
		for (const QString &s: derived().to_string_list(d)) {
			if (is_invalid_value(s))
				continue;
			increment_count_bin(value_bins, s);
		}
	}

	// Now, turn that into our result array with allocated bin objects.
	std::vector<StatsBinCount> res;
	res.reserve(value_bins.size());
	for (const Pair &pair: value_bins)
		res.push_back({ std::make_unique<Bin>(pair.first), pair.second});
	return res;
}

// ============ The date of the dive by year, quarter or month ============
// (Note that calendar week is defined differently in different parts of the world and therefore omitted for now)

double date_to_double(int year, int month, int day)
{
	struct tm tm = { 0 };
	tm.tm_year = year;
	tm.tm_mon = month;
	tm.tm_mday = day;
	timestamp_t t = utc_mktime(&tm);
	return t / 86400.0; // Turn seconds since 1970 to days since 1970, if that makes sense...?
}

struct DateYearBinner : public IntBinner<DateYearBinner, IntBin> {
	QString name() const override {
		return StatsTranslations::tr("Yearly");
	}
	QString format(const StatsBin &bin) const override {
		return QString::number(derived_bin(bin).value);
	}
	int to_bin_value(const dive *d) const {
		return utc_year(d->when);
	}
	double lowerBoundToFloatBase(int year) const {
		return date_to_double(year, 0, 0);
	}
};

using DateQuarterBin = SimpleBin<year_quarter>;

struct DateQuarterBinner : public SimpleContinuousBinner<DateQuarterBinner, DateQuarterBin> {
	QString name() const override {
		return StatsTranslations::tr("Quarterly");
	}
	QString format(const StatsBin &bin) const override {
		year_quarter value = derived_bin(bin).value;
		return StatsTranslations::tr("%1 Q%2").arg(QString::number(value.first),
							   QString::number(value.second));
	}
	// As histogram axis: show full year for new years and then Q2, Q3, Q4.
	QString formatLowerBound(const StatsBin &bin) const override {
		year_quarter value = derived_bin(bin).value;
		return value.second == 1
			? QString::number(value.first)
			: StatsTranslations::tr("Q%1").arg(QString::number(value.second));
	}
	double lowerBoundToFloatBase(year_quarter value) const {
		return date_to_double(value.first, (value.second - 1) * 3, 0);
	}
	year_quarter to_bin_value(const dive *d) const {
		struct tm tm;
		utc_mkdate(d->when, &tm);

		int year = tm.tm_year;
		switch (tm.tm_mon) {
		case 0 ... 2: return { year, 1 };
		case 3 ... 5: return { year, 2 };
		case 6 ... 8: return { year, 3 };
		default:      return { year, 4 };
		}
	}
	void inc(DateQuarterBin &bin) const {
		if (++bin.value.second > 4) {
			bin.value.second = 1;
			++bin.value.first;
		}
	}
};

using DateMonthBin = SimpleBin<year_month>;

struct DateMonthBinner : public SimpleContinuousBinner<DateMonthBinner, DateMonthBin> {
	QString name() const override {
		return StatsTranslations::tr("Monthly");
	}
	QString format(const StatsBin &bin) const override {
		year_month value = derived_bin(bin).value;
		return StatsTranslations::tr("%1 %2").arg(QString(monthname(value.second)),
							  QString::number(value.first));
	}
	double lowerBoundToFloatBase(year_quarter value) const {
		return date_to_double(value.first, value.second, 0);
	}
	year_month to_bin_value(const dive *d) const {
		struct tm tm;
		utc_mkdate(d->when, &tm);
		return { tm.tm_year, tm.tm_mon };
	}
	void inc(DateMonthBin &bin) const {
		if (++bin.value.second > 11) {
			bin.value.second = 0;
			++bin.value.first;
		}
	}
};

static DateYearBinner date_year_binner;
static DateQuarterBinner date_quarter_binner;
static DateMonthBinner date_month_binner;
struct DateType : public StatsTypeTemplate<StatsType::Type::Discrete> {
	QString name() const {
		return StatsTranslations::tr("Date");
	}
	std::vector<const StatsBinner *> binners() const override {
		return { &date_year_binner, &date_quarter_binner, &date_month_binner };
	}
};

// ============ Dive depth, binned in 5, 10, 20 m or 15, 30, 60 ft bins ============

struct MeterBinner : public IntRangeBinner<MeterBinner, IntBin> {
	using IntRangeBinner::IntRangeBinner;
	QString name() const override {
		QLocale loc;
		return StatsTranslations::tr("in %1 %2 steps").arg(loc.toString(bin_size),
								   get_depth_unit());
	}
	QString unitSymbol() const override {
		return get_depth_unit();
	}
	int to_bin_value(const dive *d) const {
		return d->maxdepth.mm / 1000 / bin_size;
	}
};

struct FeetBinner : public IntRangeBinner<FeetBinner, IntBin> {
	using IntRangeBinner::IntRangeBinner;
	QString name() const override {
		QLocale loc;
		return StatsTranslations::tr("in %1 %2 steps").arg(loc.toString(bin_size),
								   get_depth_unit());
	}
	QString unitSymbol() const override {
		return get_depth_unit();
	}
	int to_bin_value(const dive *d) const {
		return lrint(mm_to_feet(d->maxdepth.mm)) / bin_size;
	}
};

static MeterBinner meter_binner5(5);
static MeterBinner meter_binner10(10);
static MeterBinner meter_binner20(20);
static FeetBinner feet_binner15(15);
static FeetBinner feet_binner30(30);
static FeetBinner feet_binner60(60);
struct DepthType : public StatsTypeTemplate<StatsType::Type::Numeric> {
	QString name() const override {
		return StatsTranslations::tr("Max. Depth");
	}
	QString unitSymbol() const override {
		return get_depth_unit();
	}
	int decimals() const override {
		return 1;
	}
	std::vector<const StatsBinner *> binners() const override {
		if (prefs.units.length == units::METERS)
			return { &meter_binner5, &meter_binner10, &meter_binner20 };
		else
			return { &feet_binner15, &feet_binner30, &feet_binner60 };
	}
	double toFloat(const dive *d) const override {
		return prefs.units.length == units::METERS ? d->maxdepth.mm / 1000.0
							   : mm_to_feet(d->maxdepth.mm);
	}
	std::vector<StatsOperation> supportedOperations() const override {
		return { StatsOperation::Median, StatsOperation::Average, StatsOperation::Sum };
	}
};

// ============ Bottom time, binned in 5, 10, 30 min or 1 h bins ============

struct MinuteBinner : public IntRangeBinner<MinuteBinner, IntBin> {
	using IntRangeBinner::IntRangeBinner;
	QString name() const override {
		return StatsTranslations::tr("in %1 min steps").arg(bin_size);
	}
	QString unitSymbol() const override {
		return StatsTranslations::tr("min");
	}
	int to_bin_value(const dive *d) const {
		return d->duration.seconds / 60 / bin_size;
	}
};

struct HourBinner : public IntBinner<HourBinner, IntBin> {
	QString name() const override {
		return StatsTranslations::tr("in hours");
	}
	QString format(const StatsBin &bin) const override {
		return QString::number(derived_bin(bin).value);
	}
	QString unitSymbol() const override {
		return StatsTranslations::tr("h");
	}
	int to_bin_value(const dive *d) const {
		return d->duration.seconds / 3600;
	}
	double lowerBoundToFloatBase(int hour) const {
		return static_cast<double>(hour);
	}
};

static MinuteBinner minute_binner5(5);
static MinuteBinner minute_binner10(10);
static MinuteBinner minute_binner30(30);
static HourBinner hour_binner;
struct DurationType : public StatsTypeTemplate<StatsType::Type::Numeric> {
	QString name() const override {
		return StatsTranslations::tr("Duration");
	}
	QString unitSymbol() const override {
		return StatsTranslations::tr("min");
	}
	int decimals() const override {
		return 0;
	}
	std::vector<const StatsBinner *> binners() const override {
		return { &minute_binner5, &minute_binner10, &minute_binner30, &hour_binner };
	}
	double toFloat(const dive *d) const override {
		return d->duration.seconds / 60.0;
	}
	std::vector<StatsOperation> supportedOperations() const override {
		return { StatsOperation::Median, StatsOperation::Average, StatsOperation::Sum };
	}
};

// ============ SAC, binned in 2, 5, 10 l/min or 0.1, 0.2, 0.4, 0.8 cuft/min bins ============

struct MetricSACBinner : public IntRangeBinner<MetricSACBinner, IntBin> {
	using IntRangeBinner::IntRangeBinner;
	QString name() const override {
		QLocale loc;
		return StatsTranslations::tr("in %1 %2/min steps").arg(loc.toString(bin_size),
								       get_volume_unit());
	}
	QString unitSymbol() const override {
		return get_volume_unit() + StatsTranslations::tr("/min");
	}
	int to_bin_value(const dive *d) const {
		if (d->sac <= 0)
			return invalid_value<int>();
		return d->sac / 1000 / bin_size;
	}
};

// "Imperial" SACs are annoying, since we have to bin to sub-integer precision.
// We store cuft * 100 as an integer, to avoid troubles with floating point semantics.

struct ImperialSACBinner : public IntBinner<ImperialSACBinner, IntBin> {
	int bin_size;
	ImperialSACBinner(double size)
		: bin_size(lrint(size * 100.0))
	{
	}
	QString name() const override {
		QLocale loc;
		return StatsTranslations::tr("in %1 %2/min steps").arg(loc.toString(bin_size / 100.0, 'f', 2),
								       get_volume_unit());
	}
	QString format(const StatsBin &bin) const override {
		int value = derived_bin(bin).value;
		QLocale loc;
		return StatsTranslations::tr("%1–%2").arg(loc.toString((value * bin_size) / 100.0, 'f', 2),
							  loc.toString(((value + 1) * bin_size) / 100.0, 'f', 2));
	}
	QString unitSymbol() const override {
		return get_volume_unit() + StatsTranslations::tr("/min");
	}
	QString formatLowerBound(const StatsBin &bin) const override {
		int value = derived_bin(bin).value;
		return QStringLiteral("%L1").arg((value * bin_size) / 100.0, 0, 'f', 2);
	}
	double lowerBoundToFloatBase(int value) const {
		return static_cast<double>((value * bin_size) / 100.0);
	}
	int to_bin_value(const dive *d) const {
		if (d->sac <= 0)
			return invalid_value<int>();
		return lrint(ml_to_cuft(d->sac) * 100.0) / bin_size;
	}
};

MetricSACBinner metric_sac_binner2(2);
MetricSACBinner metric_sac_binner5(5);
MetricSACBinner metric_sac_binner10(10);
ImperialSACBinner imperial_sac_binner1(0.1);
ImperialSACBinner imperial_sac_binner2(0.2);
ImperialSACBinner imperial_sac_binner4(0.4);
ImperialSACBinner imperial_sac_binner8(0.8);

struct SACType : public StatsTypeTemplate<StatsType::Type::Numeric> {
	QString name() const override {
		return StatsTranslations::tr("SAC");
	}
	QString unitSymbol() const override {
		return get_volume_unit() + StatsTranslations::tr("/min");
	}
	int decimals() const override {
		return prefs.units.volume == units::LITER ? 0 : 2;
	}
	std::vector<const StatsBinner *> binners() const override {
		if (prefs.units.volume == units::LITER)
			return { &metric_sac_binner2, &metric_sac_binner5, &metric_sac_binner10 };
		else
			return { &imperial_sac_binner1, &imperial_sac_binner2, &imperial_sac_binner4, &imperial_sac_binner8 };
	}
	double toFloat(const dive *d) const override {
		if (d->sac <= 0)
			return invalid_value<double>();
		return prefs.units.volume == units::LITER ? d->sac / 1000.0 :
							    ml_to_cuft(d->sac);
	}
	std::vector<StatsOperation> supportedOperations() const override {
		return { StatsOperation::Median, StatsOperation::Average, StatsOperation::TimeWeightedAverage };
	}
};

// ============ Dive mode ============

struct DiveModeBinner : public SimpleBinner<DiveModeBinner, IntBin> {
	QString format(const StatsBin &bin) const override {
		return QString(divemode_text_ui[derived_bin(bin).value]);
	}
	int to_bin_value(const dive *d) const {
		int res = (int)d->dc.divemode;
		return res >= 0 && res < NUM_DIVEMODE ? res : OC;
	}
};

static DiveModeBinner dive_mode_binner;
struct DiveModeType : public StatsTypeTemplate<StatsType::Type::Discrete> {
	QString name() const override {
		return StatsTranslations::tr("Dive mode");
	}
	std::vector<const StatsBinner *> binners() const override {
		return { &dive_mode_binner };
	}
};

// ============ Buddy (including dive guides) ============

struct BuddyBinner : public StringBinner<BuddyBinner, StringBin> {
	std::vector<QString> to_string_list(const dive *d) const {
		std::vector<QString> dive_people;
		for (const QString &s: QString(d->buddy).split(",", SKIP_EMPTY))
			dive_people.push_back(s.trimmed());
		for (const QString &s: QString(d->divemaster).split(",", SKIP_EMPTY))
			dive_people.push_back(s.trimmed());
		return dive_people;
	}
};

static BuddyBinner buddy_binner;
struct BuddyType : public StatsTypeTemplate<StatsType::Type::Discrete> {
	QString name() const override {
		return StatsTranslations::tr("Buddies");
	}
	std::vector<const StatsBinner *> binners() const override {
		return { &buddy_binner };
	}
};

// ============ Suit  ============

struct SuitBinner : public StringBinner<SuitBinner, StringBin> {
	std::vector<QString> to_string_list(const dive *d) const {
		return { QString(d->suit) };
	}
};

static SuitBinner suit_binner;
struct SuitType : public StatsTypeTemplate<StatsType::Type::Discrete> {
	QString name() const override {
		return StatsTranslations::tr("Suit type");
	}
	std::vector<const StatsBinner *> binners() const override {
		return { &suit_binner };
	}
};

// ============ Location (including trip location)  ============

using LocationBin = SimpleBin<const dive_site *>;

struct LocationBinner : public SimpleBinner<LocationBinner, LocationBin> {
	QString format(const StatsBin &bin) const override {
		const dive_site *ds = derived_bin(bin).value;
		return QString(ds ? ds->name : "-");
	}
	const dive_site *to_bin_value(const dive *d) const {
		return d->dive_site;
	}
};

static LocationBinner location_binner;
struct LocationType : public StatsTypeTemplate<StatsType::Type::Discrete> {
	QString name() const override {
		return StatsTranslations::tr("Dive site");
	}
	std::vector<const StatsBinner *> binners() const override {
		return { &location_binner };
	}
};

static DateType date_type;
static DepthType depth_type;
static DurationType duration_type;
static SACType sac_type;
static DiveModeType dive_mode_type;
static BuddyType buddy_type;
static SuitType suit_type;
static LocationType location_type;
const std::vector<const StatsType *> stats_types = {
	&date_type, &depth_type, &duration_type, &sac_type, &dive_mode_type, &buddy_type, &suit_type, &location_type
};

const std::vector<const StatsType *> stats_continuous_types = {
	&date_type, &depth_type, &duration_type, &sac_type
};

const std::vector<const StatsType *> stats_numeric_types = {
	&depth_type, &duration_type, &sac_type
};
