#include "config.h"
#include <iostream>
namespace sylar {

// Config::ConfigVarMap Config::s_datas;

ConfigVarBase::ptr Config::LookupBase(const std::string &name)
{
    RWMutex::ReadLock lock(GetMutex());
    auto it = GetDatas().find(name);
    return it == GetDatas().end() ? nullptr : it->second;
}

static void ListAllMember(const std::string& prefix,
                          const YAML::Node& node,
                          std::list<std::pair<std::string, const YAML::Node>>& output)
{
    if (prefix.find_first_not_of("abcdefghijklmnopqrstuvwxyz._0123456789") != std::string::npos) {
        SYLAR_LOG_ERROR(SYLAR_LOGGER_ROOT()) << "Config invalid name: " << prefix << " : " << node;
        return;
    }
    output.emplace_back(std::make_pair(prefix, node));
    if (node.IsMap()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            // 递归调用，以“."表示从属关系
            ListAllMember(prefix.empty() ? it->first.Scalar()
                        : prefix + "." + it->first.Scalar(), it->second, output);
        }
    }
}

void Config::LoadFromYaml(const YAML::Node &root)
{
    // all_nodes用来存放配置项（即yaml中对象的索引）与对应的值的映射表
    std::list<std::pair<std::string, const YAML::Node>> all_nodes;
    ListAllMember("", root, all_nodes);

    for (auto& i: all_nodes) {
        std::string key = i.first;
        if (key.empty()) {
            continue;
        }
        // 把名字都改成小写
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        ConfigVarBase::ptr var = LookupBase(key);

        if (var) {
            if (i.second.IsScalar()) {
                var->fromString(i.second.Scalar());
            } else {
                std::stringstream  ss;
                ss << i.second;
                var->fromString(ss.str());
            }

        }
    }


}

void Config::Visit(const std::function<void(ConfigVarBase::ptr)>& cb)
{
    RWMutex::ReadLock lock(GetMutex());
    ConfigVarMap& m = GetDatas();
    for (auto&& d: m) {
        cb(d.second);
    }
}

}