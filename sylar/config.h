#ifndef __SYLAR_CONFIG_H__
#define __SYLAR_CONFIG_H__

#include <memory>
#include <string>
#include <sstream>
#include <typeinfo>
#include <unordered_map>
#include <list>
#include <functional>
#include <boost/lexical_cast.hpp>
#include "log.h"
#include "yaml-cpp/yaml.h"
#include "thread.h"

namespace sylar {

class ConfigVarBase {
public:
    typedef std::shared_ptr<ConfigVarBase> ptr;
    explicit ConfigVarBase(const std::string& name, const std::string& description = "")
        : m_name(name)
        , m_description(description)
    {}
    virtual ~ConfigVarBase() = default;

    const std::string& getName() const { return m_name; }
    const std::string& getDescription() const { return m_description; }

    virtual std::string toString() = 0;
    virtual bool fromString(const std::string& val) = 0;
    virtual std::string getTypename() = 0;



private:
    std::string m_name;
    std::string m_description;
};

// 原始的模板类
// F: From_type,  T: To_type
template<class F, class T>
class LexicalCast {
public:
    T operator() (const F& v)
    {
        return boost::lexical_cast<T> (v);
    }
};

// 偏特化从string转为vector<T>
template<class T>
class LexicalCast<std::string, std::vector<T>> {
public:
    std::vector<T> operator() (const std::string& v)
    {
        // 先把string中的内容加载到node中，在把node中每个节点的内容转成string后再转成T,然后加载到vec中
        YAML::Node node = YAML::Load(v);
        typename std::vector<T> vec;
        std::stringstream ss;
        for (auto && i : node) {
            ss.str("");
            ss << i;
            vec.push_back(LexicalCast<std::string, T>() (ss.str()));
        }
        return  vec;
    }
};

// 偏特化从vector<T>转为string
template<class T>
class LexicalCast<std::vector<T>, std::string> {
public:
    std::string operator() (const std::vector<T>& v) {
        YAML::Node node;
        for (auto& i: v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>() (i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

// 偏特化从string转为list<T>
template<class T>
class LexicalCast<std::string, std::list<T>> {
public:
    std::list<T> operator() (const std::string& v)
    {
        // 先把string中的内容加载到node中，在把node中每个节点的内容转成string后再转成T,然后加载到vec中
        YAML::Node node = YAML::Load(v);
        typename std::list<T> lis;
        std::stringstream ss;
        for (auto && i : node) {
            ss.str("");
            ss << i;
            lis.push_back(LexicalCast<std::string, T>() (ss.str()));
        }
        return  lis;
    }
};

// 偏特化从list<T>转为string
template<class T>
class LexicalCast<std::list<T>, std::string> {
public:
    std::string operator() (const std::list<T>& v) {
        YAML::Node node;
        for (auto& i: v) {
            node.push_back(YAML::Load(LexicalCast<T, std::string>() (i)));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};

// 偏特化从string转为unordered_map<T>
template<class T>
class LexicalCast<std::string, std::unordered_map<std::string, T>> {
public:
    std::vector<T> operator() (const std::string& v)
    {
        // 先把string中的内容加载到node中，在把node中每个节点的内容转成string后再转成T,然后加载到vec中
        YAML::Node node = YAML::Load(v);
        typename std::vector<T> vec;
        std::stringstream ss;
        for (auto it: node) {
            ss.str("");
            ss << it.second;
            vec.insert(std::make_pair(it.first.Scalar(), LexicalCast<std::string, T>() (ss.str())));
        }
        return  vec;
    }
};

// 偏特化从unordered_map转为string
template<class T>
class LexicalCast<std::unordered_map<std::string, T>, std::string> {
public:
    std::string operator() (const std::unordered_map<std::string, T>& v) {
        YAML::Node node;
        for (auto& i: v) {
            node[i.first] = YAML::Load(LexicalCast<T, std::string>() (i.second));
        }
        std::stringstream ss;
        ss << node;
        return ss.str();
    }
};


template<class T, class FromStr = LexicalCast<std::string, T>,
                  class ToStr = LexicalCast<T, std::string> >
class ConfigVar: public ConfigVarBase {
public:
    typedef std::shared_ptr<ConfigVar> ptr;
    typedef std::function<void (const T& old_value, const T& new_value)> on_change_cb;

    ConfigVar(const std::string& name
            , const T& val
            , const std::string& description = "")
            : ConfigVarBase(name, description)
            , m_val(val)
    {}

    std::string toString() override 
    {
        try {
            RWMutex::ReadLock lock(m_mutex);
            return ToStr()(m_val);
            //return boost::lexical_cast<std::string> (m_val);
        } catch (std::exception& e) {
            SYLAR_LOG_ERROR(SYLAR_LOGGER_ROOT()) << "ConfigVar::toString exception "
                << e.what() << "convert: " << typeid(m_val).name() << " to string";
        }
        return "";
    }

    bool fromString(const std::string& val) override
    {
        try {
            setValue(FromStr()(val));
            //m_val = boost::lexical_cast<T> (val);
            return true;
        } catch (std::exception& e) {
            SYLAR_LOG_ERROR(SYLAR_LOGGER_ROOT()) << "ConfigVar::fromString exception "
                << e.what() << "convert: " << "string to " << typeid(m_val).name();
        }
        return false;
    }
    const T& getValue() const
    {
        RWMutex::ReadLock lock(m_mutex);
        return m_val;
    }
    void setValue(const T& new_val)
    {
        {
            RWMutex::ReadLock lock(m_mutex);
            if (new_val == m_val) {
                return;
            }
            for (auto &i: m_cbs) {
                i.second(m_val, new_val);
            }
        }
        RWMutex::WriteLock lock(m_mutex);
        m_val = new_val;
    }

    std::string getTypename() override
    {
        return typeid(T).name();
    }

    void addListener(on_change_cb cb)
    {
        static uint64_t s_fun_id = 0;
        RWMutex::WriteLock lock(m_mutex);
        ++s_fun_id;
        m_cbs[s_fun_id] = cb;
    }
    void delListener(uint64_t key)
    {
        RWMutex::WriteLock lock(m_mutex);
        m_cbs.erase(key);
    }
    on_change_cb getListener(uint64_t key)
    {
        RWMutex::ReadLock lock(m_mutex);
        auto it = m_cbs.find(key);
        return it == m_cbs.end() ? nullptr : it->second;
    }
    void clearListener()
    {
        m_cbs.clear();
    }
private:
    T m_val;
    std::unordered_map<uint64_t, on_change_cb> m_cbs;
    mutable RWMutex m_mutex;

};


class Config {
public:
    typedef std::unordered_map<std::string, ConfigVarBase::ptr> ConfigVarMap;

    template<class T>
    static typename ConfigVar<T>::ptr Lookup(const std::string& name, 
                                            const T& default_value, 
                                            const std::string& descripton = "")
    {
        RWMutex::WriteLock lock(GetMutex());
        auto it = GetDatas().find(name);
        // 如果查得到
        if (it != GetDatas().end()) {
            auto tmp = std::dynamic_pointer_cast<ConfigVar<T>>(it->second);
            if (tmp) {
                SYLAR_LOG_INFO(SYLAR_LOGGER_ROOT()) << "Lookup name=" << name << " exists";
                return tmp;
            } else {
                SYLAR_LOG_ERROR(SYLAR_LOGGER_ROOT()) << "Lookup name=" << name << " exists but type not " <<
                    typeid(T).name() << " real type=" << it->second->getTypename() << " : " << it->second->toString();
            }
        }

        // 查不到则将其插入（先检查命名规范性）
        if (name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ._0123456789")
            != std::string::npos)
        {
            SYLAR_LOG_ERROR(SYLAR_LOGGER_ROOT()) << "Lookup name invalid " << name;
            throw std::invalid_argument(name);
        }

        typename ConfigVar<T>::ptr v(new ConfigVar<T>(name, default_value, descripton));
        GetDatas()[name] = v;
        return v;
    }

    template<class T>
    static typename ConfigVar<T>::ptr Lookup(const std::string& name)
    { 
        RWMutex::ReadLock lock(GetMutex());
        auto it = GetDatas().find(name);
        if (it == GetDatas().end()) {
            return nullptr;
        }
        // 由于unordered_map里存的是基类指针ConfigVarBase，所以需要转成派生类ConfigVar
        return std::dynamic_pointer_cast<ConfigVar<T>> (it->second);
    }

    // 静态成员函数，为了让该函数与类对象无关且又能访问类的私有成员(s_datas)
    static ConfigVarBase::ptr LookupBase(const std::string& name);

    static void LoadFromYaml(const YAML::Node& root);

    static void Visit(const std::function<void(ConfigVarBase::ptr)>&);


private:

    // 为了解决静态变量初始化顺序问题（调用静态函数lookup的时候用到的s_datas应当已经初始化，不然会出问题）
    static ConfigVarMap& GetDatas()
    {
        static ConfigVarMap s_datas;
        return s_datas;
    }
    // 同样为了解决静态变量初始化次序问题
    static RWMutex& GetMutex()
    {
        static RWMutex s_mutex;
        return s_mutex;
    }
};

}


#endif