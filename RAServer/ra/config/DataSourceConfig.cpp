#include <ra/common/Json.h>
#include <ra/config/DataSourceConfig.h>
#include <ra/util/Util.h>
#include <ra/util/SqlData.h>
#include <contrib/fetcher/MetricFetcherFactory.h>

RA_BEGIN_NAMESPACE(config);
using namespace std;
RA_USE_NAMESPACE(common);
RA_USE_NAMESPACE(util);
RA_USE_NAMESPACE(fetcher);

DataSourceConfig::DataSourceConfig()
{
}

DataSourceConfig::~DataSourceConfig() {
    clear();
}

bool DataSourceConfig::loadConfig(const string& configFilePath)
{
    SqlData sqlData(configFilePath);
    const char* columns[] = { "id", "address", "service", "fetcherType", "optionString", "author", "modifiedTime" };
    int32_t columnCount = sizeof(columns)/sizeof(columns[0]);
    bool ret = sqlData.load(TABLE_NAME_DATA_SOURCE, vector<string>(columns, columns + columnCount));
    if (!ret) {
        LOG(ERROR) << "load table[" << TABLE_NAME_DATA_SOURCE << "] failed";
        return false;
    }
    int32_t rowNum = sqlData.getRow();
    int32_t colNum = sqlData.getCol();
    if (rowNum == 0) {
        LOG(WARNING) << "table[" << TABLE_NAME_DATA_SOURCE << "] is empty";
        return true;
    }
    if (columnCount != colNum) {
        LOG(ERROR) << "table[" << TABLE_NAME_DATA_SOURCE << "] format is illegal, colNum[" 
		   << colNum << "] is not 5";
        return false;
    }

    clear();
    ScopedLock lock(_mutex);
    vector<string> rowVals;
    for (int32_t rowIndex = 0; rowIndex < rowNum; ++rowIndex) {
        ret = sqlData.getRow(rowIndex, rowVals);
        if (!ret) {
            LOG(ERROR) << "read table[" << TABLE_NAME_DATA_SOURCE << "] row failed";
            return false;
        }
        uint32_t id = 0;
        Util::fromString(rowVals[0], id);
        const string& address = rowVals[1];
        const string& serviceName = rowVals[2];
        const string& fetcherType = rowVals[3];
        const string& optionString = rowVals[4];
        JsonPtr json;
        if (optionString.empty()) {
            json = JsonPtr(new JsonObject());
        } else {
            json = Json::load(optionString);
        }
        OptionMap options;
        if (!fromJson(json, options)) {
            LOG(ERROR) << "cannot read fetcher options from json string '" << optionString << "'";
            return false;
        }
        if (options.find(FETCHER_OPTION_NAME_KEY) == options.end()) options[FETCHER_OPTION_NAME_KEY] = serviceName;
        if (options.find(FETCHER_OPTION_TYPE_KEY) == options.end()) options[FETCHER_OPTION_TYPE_KEY] = fetcherType;
        if (options.find(FETCHER_OPTION_ADDR_KEY) == options.end()) options[FETCHER_OPTION_ADDR_KEY] = address;
        MetricFetcherPtr fetcher = MetricFetcherFactory::create(fetcherType);
        if (fetcher == NULL || !fetcher->init(options)) {
            LOG(ERROR) << "cannot create and initialize fetcher, addr: "
		       << address << ", type: " << fetcherType;
            return false;
        }
        HostItem item(address, serviceName, id);
        _hostVec.push_back(item);
        _metricFetcherMap[address] = fetcher;
    }

    if (_hostVec.size() != _metricFetcherMap.size()) {
        LOG(ERROR) << "There are duplicate fetcher specs in fetcher table, load failed";
        return false;
    }

    return true;
}

void DataSourceConfig::clear()
{
    ScopedLock lock(_mutex);
    MetricFetcherMap::iterator iter;
    for (iter = _metricFetcherMap.begin(); iter != _metricFetcherMap.end(); iter++) {
        MetricFetcherPtr& fetcher = iter->second;
        fetcher->close();
    }
    _metricFetcherMap.clear();
    _hostVec.clear();
}

MetricFetcherPtr DataSourceConfig::getMetricFetcher(const std::string& spec) const {
    ScopedLock lock(_mutex);
    MetricFetcherMap::const_iterator it = _metricFetcherMap.find(spec);
    if (it == _metricFetcherMap.end()) {
        LOG(ERROR) << "request illegal fetcher spec[" << spec << "]";
        return MetricFetcherPtr();
    }
    return it->second;
}

void DataSourceConfig::getSpecs(set<string>& specSet) const
{
    specSet.clear();
    MetricFetcherMap::const_iterator it = _metricFetcherMap.begin();
    while (it != _metricFetcherMap.end()) {
        const string& amonSpec = it->first;
        specSet.insert(amonSpec);
        ++it;
    }
}

RA_END_NAMESPACE(config);

