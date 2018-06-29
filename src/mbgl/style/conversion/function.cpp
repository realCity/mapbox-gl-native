#include <mbgl/style/conversion/function.hpp>
#include <mbgl/style/expression/dsl.hpp>
#include <mbgl/style/expression/step.hpp>
#include <mbgl/style/expression/interpolate.hpp>
#include <mbgl/style/expression/match.hpp>
#include <mbgl/util/string.hpp>

namespace mbgl {
namespace style {
namespace conversion {

using namespace expression;
using namespace expression::dsl;

enum class FunctionType {
    Interval,
    Exponential,
    Categorical,
    Identity,
    Invalid
};

FunctionType functionType(type::Type type, const Convertible& value) {
    bool interpolatable = type.match(
        [&] (const type::NumberType&) {
            return true;
        },
        [&] (const type::ColorType&) {
            return true;
        },
        [&] (const type::Array& array) {
            return array.N && array.itemType == type::Number;
        },
        [&] (const auto&) {
            return false;
        }
    );

    auto typeValue = objectMember(value, "type");
    if (!typeValue) {
        return interpolatable ? FunctionType::Exponential : FunctionType::Interval;
    }

    optional<std::string> string = toString(*typeValue);
    if (!string) {
        return FunctionType::Invalid;
    }

    if (*string == "interval") return FunctionType::Interval;
    if (*string == "exponential") return FunctionType::Exponential;
    if (*string == "categorical") return FunctionType::Categorical;
    if (*string == "identity") return FunctionType::Identity;

    return FunctionType::Invalid;
}

optional<std::unique_ptr<Expression>> convertLiteral(type::Type type, const Convertible& value, Error& error) {
    return type.match(
        [&] (const type::NumberType&) -> optional<std::unique_ptr<Expression>> {
            auto result = convert<float>(value, error);
            if (!result) {
                return {};
            }
            return literal(double(*result));
        },
        [&] (const type::BooleanType&) -> optional<std::unique_ptr<Expression>> {
            auto result = convert<bool>(value, error);
            if (!result) {
                return {};
            }
            return literal(*result);
        },
        [&] (const type::StringType&) -> optional<std::unique_ptr<Expression>> {
            auto result = convert<std::string>(value, error);
            if (!result) {
                return {};
            }
            return literal(*result);
        },
        [&] (const type::ColorType&) -> optional<std::unique_ptr<Expression>> {
            auto result = convert<Color>(value, error);
            if (!result) {
                return {};
            }
            return literal(*result);
        },
        [&] (const type::Array& array) -> optional<std::unique_ptr<Expression>> {
            if (!isArray(value)) {
                error = { "value must be an array" };
                return {};
            }
            if (array.N && arrayLength(value) != *array.N) {
                error = { "value must be an array of length " + util::toString(*array.N) };
                return {};
            }
            return array.itemType.match(
                [&] (const type::NumberType&) -> optional<std::unique_ptr<Expression>> {
                    std::vector<expression::Value> result;
                    result.reserve(arrayLength(value));
                    for (std::size_t i = 0; i < arrayLength(value); ++i) {
                        optional<float> number = toNumber(arrayMember(value, i));
                        if (!number) {
                            error = { "value must be an array of numbers" };
                            return {};
                        }
                        result.push_back(double(*number));
                    }
                    return literal(result);
                },
                [&] (const type::StringType&) -> optional<std::unique_ptr<Expression>> {
                    std::vector<expression::Value> result;
                    result.reserve(arrayLength(value));
                    for (std::size_t i = 0; i < arrayLength(value); ++i) {
                        optional<std::string> string = toString(arrayMember(value, i));
                        if (!string) {
                            error = { "value must be an array of strings" };
                            return {};
                        }
                        result.push_back(*string);
                    }
                    return literal(result);
                },
                [&] (const auto&) -> optional<std::unique_ptr<Expression>> {
                    assert(false); // No properties use this type.
                    return {};
                }
            );
        },
        [&] (const type::NullType&) -> optional<std::unique_ptr<Expression>> {
            assert(false); // No properties use this type.
            return {};
        },
        [&] (const type::ObjectType&) -> optional<std::unique_ptr<Expression>> {
            assert(false); // No properties use this type.
            return {};
        },
        [&] (const type::ErrorType&) -> optional<std::unique_ptr<Expression>> {
            assert(false); // No properties use this type.
            return {};
        },
        [&] (const type::ValueType&) -> optional<std::unique_ptr<Expression>> {
            assert(false); // No properties use this type.
            return {};
        }
    );
}

optional<std::map<double, std::unique_ptr<Expression>>> convertStops(type::Type type,
                                                                     const Convertible& value,
                                                                     Error& error) {
    auto stopsValue = objectMember(value, "stops");
    if (!stopsValue) {
        error = { "function value must specify stops" };
        return {};
    }

    if (!isArray(*stopsValue)) {
        error = { "function stops must be an array" };
        return {};
    }

    if (arrayLength(*stopsValue) == 0) {
        error = { "function must have at least one stop" };
        return {};
    }

    std::map<double, std::unique_ptr<Expression>> stops;
    for (std::size_t i = 0; i < arrayLength(*stopsValue); ++i) {
        const auto& stopValue = arrayMember(*stopsValue, i);

        if (!isArray(stopValue)) {
            error = { "function stop must be an array" };
            return {};
        }

        if (arrayLength(stopValue) != 2) {
            error = { "function stop must have two elements" };
            return {};
        }

        optional<float> d = convert<float>(arrayMember(stopValue, 0), error);
        if (!d) {
            return {};
        }

        optional<std::unique_ptr<Expression>> r = convertLiteral(type, arrayMember(stopValue, 1), error);
        if (!r) {
            return {};
        }

        stops.emplace(*d, std::move(*r));
    }

    return stops;
}

template <class T, class D = T>
optional<std::unordered_map<D, std::shared_ptr<Expression>>> convertBranches(type::Type type,
                                                                             const Convertible& value,
                                                                             Error& error) {
    auto stopsValue = objectMember(value, "stops");
    if (!stopsValue) {
        error = { "function value must specify stops" };
        return {};
    }

    if (!isArray(*stopsValue)) {
        error = { "function stops must be an array" };
        return {};
    }

    if (arrayLength(*stopsValue) == 0) {
        error = { "function must have at least one stop" };
        return {};
    }

    std::unordered_map<D, std::shared_ptr<Expression>> stops;
    for (std::size_t i = 0; i < arrayLength(*stopsValue); ++i) {
        const auto& stopValue = arrayMember(*stopsValue, i);

        if (!isArray(stopValue)) {
            error = { "function stop must be an array" };
            return {};
        }

        if (arrayLength(stopValue) != 2) {
            error = { "function stop must have two elements" };
            return {};
        }

        optional<T> d = convert<T>(arrayMember(stopValue, 0), error);
        if (!d) {
            return {};
        }

        optional<std::unique_ptr<Expression>> r = convertLiteral(type, arrayMember(stopValue, 1), error);
        if (!r) {
            return {};
        }

        stops.emplace(D(*d), std::move(*r));
    }

    return stops;
}

optional<std::unique_ptr<Expression>> convertIntervalFunction(type::Type type,
                                                              const Convertible& value,
                                                              Error& error,
                                                              std::unique_ptr<Expression> input) {
    auto stops = convertStops(type, value, error);
    if (!stops) {
        return {};
    }
    return std::unique_ptr<Expression>(
        std::make_unique<Step>(type, std::move(input), std::move(*stops)));
}

optional<std::unique_ptr<Expression>> convertExponentialFunction(type::Type type,
                                                                 const Convertible& value,
                                                                 Error& error,
                                                                 std::unique_ptr<Expression> input) {
    auto stops = convertStops(type, value, error);
    if (!stops) {
        return {};
    }

    auto base = 1.0f;
    auto baseValue = objectMember(value, "base");
    if (baseValue && toNumber(*baseValue)) {
        base = *toNumber(*baseValue);
    }

    ParsingContext ctx;
    auto result = createInterpolate(type, exponential(base), std::move(input), std::move(*stops), ctx);
    if (!result) {
        assert(false);
        return {};
    }

    return std::move(*result);
}

optional<std::unique_ptr<Expression>> convertCategoricalFunction(type::Type type,
                                                                 const Convertible& value,
                                                                 Error& err,
                                                                 std::unique_ptr<Expression> input) {
    auto stopsValue = objectMember(value, "stops");
    if (!stopsValue) {
        err = { "function value must specify stops" };
        return {};
    }

    if (!isArray(*stopsValue)) {
        err = { "function stops must be an array" };
        return {};
    }

    if (arrayLength(*stopsValue) == 0) {
        err = { "function must have at least one stop" };
        return {};
    }

    const auto& first = arrayMember(*stopsValue, 0);

    if (!isArray(first)) {
        err = { "function stop must be an array" };
        return {};
    }

    if (arrayLength(first) != 2) {
        err = { "function stop must have two elements" };
        return {};
    }

//    if (auto b = toBool(arrayMember(first, 0))) {
//        return case_(eq(std::move(input), literal(*b)), value, error("replaced with default"));
//    }

    if (toNumber(arrayMember(first, 0))) {
        auto branches = convertBranches<float, int64_t>(type, value, err);
        if (!branches) {
            return {};
        }
        return std::unique_ptr<Expression>(
            std::make_unique<Match<int64_t>>(type, std::move(input), std::move(*branches), error("replaced with default")));
    }

    if (toString(arrayMember(first, 0))) {
        auto branches = convertBranches<std::string>(type, value, err);
        if (!branches) {
            return {};
        }
        return std::unique_ptr<Expression>(
            std::make_unique<Match<std::string>>(type, std::move(input), std::move(*branches), error("replaced with default")));
    }

    err = { "stop domain value must be a number, string, or boolean" };
    return {};
}

optional<std::unique_ptr<Expression>> convertCameraFunctionToExpression(type::Type type,
                                                                                    const Convertible& value, 
                                                                                    Error& error) {
    if (!isObject(value)) {
        error = { "function must be an object" };
        return {};
    }

    switch (functionType(type, value)) {
    case FunctionType::Interval:
        return convertIntervalFunction(type, value, error, zoom());
    case FunctionType::Exponential:
        return convertExponentialFunction(type, value, error, zoom());
    default:
        error = { "unsupported function type" };
        return {};
    }
}

optional<std::unique_ptr<Expression>> convertSourceFunctionToExpression(type::Type type,
                                                                                    const Convertible& value, 
                                                                                    Error& error) {
    if (!isObject(value)) {
        error = { "function must be an object" };
        return {};
    }

    auto propertyValue = objectMember(value, "property");
    if (!propertyValue) {
        error = { "function must specify property" };
        return {};
    }

    auto propertyString = toString(*propertyValue);
    if (!propertyString) {
        error = { "function property must be a string" };
        return {};
    }

    switch (functionType(type, value)) {
    case FunctionType::Interval:
        return convertIntervalFunction(type, value, error, number(get(literal(*propertyString))));
    case FunctionType::Exponential:
        return convertExponentialFunction(type, value, error, number(get(literal(*propertyString))));
    case FunctionType::Categorical:
        return convertCategoricalFunction(type, value, error, get(literal(*propertyString)));
    case FunctionType::Identity:
        return get(literal(*propertyString));
    default:
        error = { "unsupported function type" };
        return {};
    }
}

optional<std::unique_ptr<Expression>> convertCompositeFunctionToExpression(type::Type type,
                                                                           const Convertible& value,
                                                                           Error& error) {
    if (!isObject(value)) {
        error = { "function must be an object" };
        return {};
    }

    auto propertyValue = objectMember(value, "property");
    if (!propertyValue) {
        error = { "function must specify property" };
        return {};
    }

    auto propertyString = toString(*propertyValue);
    if (!propertyString) {
        error = { "function property must be a string" };
        return {};
    }

    switch (functionType(type, value)) {
    case FunctionType::Interval:
    case FunctionType::Exponential:
    case FunctionType::Categorical:
    default:
        error = { "unsupported function type" };
        return {};
    }
}

//template <class S>
//struct Converter<CompositeValue<S>> {
//    optional<CompositeValue<S>> operator()(const Convertible& value, Error& error) const {
//        if (!isObject(value)) {
//            error = { "stop must be an object" };
//            return {};
//        }
//
//        auto zoomValue = objectMember(value, "zoom");
//        if (!zoomValue) {
//            error = { "stop must specify zoom" };
//            return {};
//        }
//
//        auto propertyValue = objectMember(value, "value");
//        if (!propertyValue) {
//            error = { "stop must specify value" };
//            return {};
//        }
//
//        optional<float> z = convert<float>(*zoomValue, error);
//        if (!z) {
//            return {};
//        }
//
//        optional<S> s = convert<S>(*propertyValue, error);
//        if (!s) {
//            return {};
//        }
//
//        return CompositeValue<S> { *z, *s };
//    }
//};

} // namespace conversion
} // namespace style
} // namespace mbgl
