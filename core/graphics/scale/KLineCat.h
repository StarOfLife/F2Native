#ifndef XG_GRAPHICS_SCALE_CAT_KLINE_H
#define XG_GRAPHICS_SCALE_CAT_KLINE_H

#include <set>
#include <string>
#include "Category.h"
#include "../../utils/StringUtil.h"
#include "../../utils/xtime.h"

namespace xg {
namespace scale {

namespace kline {
static nlohmann::json ConfigFromLineType(std::string klineType) {
    std::vector<std::string> lineTypes;
    StringUtil::Split(klineType, lineTypes, '-');

    if(lineTypes.size() < 2) {
        // throw error.
        return {};
    }

    nlohmann::json config;

    std::string type1 = lineTypes[1];
    config["lineType"] = type1;
    if(type1 != "minutes") {
        // day/week/month
    } else {
        int minutes = std::stoi(lineTypes[2]);
        config["minutes"] = minutes;
    }

    return config;
}

} // namespace kline

class KLineCat : public Category {
  public:
    KLineCat(const std::string &_field, const nlohmann::json &_values, const nlohmann::json &config = {})
        : Category(_field, _values, config) {
        // ["kline-day", "kline-week", "kline-month", "kline-minutes-1", "kline-minutes-5", "kline-minutes-15", "kline-minutes-30", "kline-minutes-60", "kline-minutes-120"]
        
        InitConfig(config);
        nlohmann::json lineCfg = kline::ConfigFromLineType(kLineType_);
        this->PreProcessTicks();
        this->ticks = this->CalculateTicks();
    }
    
    virtual void InitConfig(const nlohmann::json &config) override {
        Category::InitConfig(config);
        kLineType_ = json::GetString(config, "klineType");
        timeZoneOffset_ = json::GetNumber(config, "timeZoneOffset");
        minutes_ = json::GetIntNumber(config, "minutes");
        dateFormate_ = json::GetString(config, "dateFormate");
    }

    ScaleType GetType() const noexcept override { return ScaleType::Kline; }

    void Change(const nlohmann::json &cfg = {}) override {
        Category::Change(cfg);
    }

  public:
    nlohmann::json CalculateTicks() override {
        if(kLineType_ == "minutes") {
            return CalculateMinutesTicks();
        } else {
            return CalculateDaysTicks();
        }
    }

  private:
    nlohmann::json CalculateDaysTicks() {
        std::size_t start = static_cast<std::size_t>(this->min);
        std::size_t end = static_cast<std::size_t>(this->max);

        std::size_t intervalStep = 1;
        if(kLineType_ == "day") {
            intervalStep = 1;
        } else if(kLineType_ == "week") {
            intervalStep = 2;
        } else if(kLineType_ == "month") {
            intervalStep = 11;
        }

        std::vector<std::size_t> indicators;
        for(std::size_t index = start; index <= end; ++index) {
            nlohmann::json &item = values[index];

            std::size_t itemHash = nlohmann::detail::hash(item);
            if(allTicksCache_.find(itemHash) != allTicksCache_.end()) {
                indicators.emplace_back(index);
            }
        }

        nlohmann::json rst;
        for(std::size_t index = 0; index < indicators.size(); index += intervalStep) {
            rst.push_back(values[indicators[index]]);
        }
        return rst;
    }

    nlohmann::json CalculateMinutesTicks() {
        std::size_t columnCount = this->GetValuesSize();
        std::size_t start = static_cast<std::size_t>(this->min);
        std::size_t end = static_cast<std::size_t>(this->max);

        std::size_t timeRange = minutes_ * columnCount;
        std::size_t timeStep = 1;
        if(timeRange <= 240) {
            // if timeRange less 4 hour, set interval delta is 30 minutes.
            timeStep = 1;
        } else if(240 < timeRange && timeRange <= 720) {
            // set interval is 60 minutes.
            timeStep = 2;
        } else {
            // set interval is 6 days.
            timeStep = 21;
        }

        std::vector<std::size_t> indicators;
        for(std::size_t index = start; index <= end; index++) {
            nlohmann::json &item = values[index];

            std::size_t itemHash = nlohmann::detail::hash(item);
            if(allTicksCache_.find(itemHash) != allTicksCache_.end()) {
                indicators.emplace_back(index);
            }
        }

        nlohmann::json rst;
        for(std::size_t index = 0; index < indicators.size(); index += timeStep) {
            rst.push_back(values[indicators[index]]);
        }
        return rst;
    }

    std::tm ConvertDataToTS(nlohmann::json &data) {
        if(data.is_string()) {
            // date str
            if(!dateFormate_.empty()) {
                return DateParserAtTM(data.get<string>(), dateFormate_);
            }
            return DateParserAtTM(data.get<string>());
        } else if(data.is_number()) {
            // timestamp
            long long t = data.get<long long>();
            long timeZoneOffset = 0;
            bool forceTimeZone = false;
            if(timeZoneOffset_ != 0) {
                timeZoneOffset = timeZoneOffset_;
                forceTimeZone = true;
            }
            timeZoneOffset *= 1000;
            return xg::DateParserTimeStamp(t + timeZoneOffset, forceTimeZone);
        } else {
            return std::tm{};
        }
    }

    void PreProcessTicks() {
        if(kLineType_ == "minutes") {
            for(std::size_t step = 0; step < values.size() - 1; step++) {
                nlohmann::json &cur = values[step];
                nlohmann::json &next = values[step + 1];

                std::tm curTime = ConvertDataToTS(cur);
                std::tm nextTime = ConvertDataToTS(next);
                if(curTime.tm_mday != nextTime.tm_mday) {
                    allTicksCache_[nlohmann::detail::hash(next)] = nextTime;
                    step++;
                } else {
                    if(curTime.tm_min % 30 == 0) {
                        allTicksCache_[nlohmann::detail::hash(cur)] = curTime;
                    } else {
                        continue;
                    }
                }
            }
        } else {
            for(std::size_t step = 0; step < values.size() - 1; step++) {
                nlohmann::json &cur = values[step];
                nlohmann::json &next = values[step + 1];

                std::tm curTime = ConvertDataToTS(cur);
                std::tm nextTime = ConvertDataToTS(next);
                if(curTime.tm_mon != nextTime.tm_mon) {
                    allTicksCache_[nlohmann::detail::hash(next)] = nextTime;
                    step++;
                }
            }
        }
    }

  private:
    std::unordered_map<std::size_t, std::tm> allTicksCache_;
    std::string kLineType_;
    std::size_t minutes_;
    long timeZoneOffset_ = 0;
    std::string dateFormate_ = "";
};
} // namespace scale
} // namespace xg

#endif // XG_GRAPHICS_SCALE_CAT_KLINE_H
