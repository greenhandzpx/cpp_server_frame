#include "../sylar/config.h" 
#include "../sylar/log.h"
#include "yaml-cpp/yaml.h"
#include <iostream>
class Person {
public:
    std::string m_name;
    int m_age;
    bool m_sex;
    Person (std::string name, int age, bool sex)
        : m_name(name)
        , m_age(age)
        , m_sex(sex)
    {}
    Person() = default;

    bool operator== (const Person& oth) const
    {
        return m_name == oth.m_name
            && m_age == oth.m_age
            && m_sex == oth.m_sex;
    }

    std::string toString() const
    {
        return "The person's name: " + m_name + ", age: " + std::to_string(m_age) + ", sex: " + (m_sex ? "male" : "female");
    }
};

Person one_person("Greenhand", 13, false);

sylar::ConfigVar<int>::ptr port_config_var = sylar::Config::Lookup("system.port", 12345, "one_ip_port");
sylar::ConfigVar<double>::ptr value_config_var = sylar::Config::Lookup("system.value", (double)25.0, "one_value");
sylar::ConfigVar<int>::ptr repeat_config_var = sylar::Config::Lookup("system.value", (int)50, "one_value");
sylar::ConfigVar<std::vector<int>>::ptr int_vec_config_var = sylar::Config::Lookup("system.int_vec", std::vector<int>{10, 20, 50}, "one_value");
sylar::ConfigVar<std::list<int>>::ptr int_list_config_var = sylar::Config::Lookup("system.int_list", std::list<int>{100, 200, 500}, "one_value");
sylar::ConfigVar<Person>::ptr person_config_var = sylar::Config::Lookup("class.person", one_person, "one_person");

void print_yaml(const YAML::Node& node, int level) 
{
    if (node.IsScalar()) {
        SYLAR_LOG_INFO(SYLAR_LOGGER_ROOT()) << std::string(level * 4, ' ')
            << node.Scalar() << " - " << node.Type() << " - " << level;
    } else if (node.IsNull()) {
        SYLAR_LOG_INFO(SYLAR_LOGGER_ROOT()) << std::string(level * 4, ' ')
            << "NULL - " << node.Tag() << " - " << level;
    } else if (node.IsMap()) {
        for (auto i = node.begin(); i != node.end(); ++i) {
            SYLAR_LOG_INFO(SYLAR_LOGGER_ROOT()) << std::string(level * 4, ' ')
                << i->first << " - " << i->second.Tag() << " - " << level;
            // 递归打印
            print_yaml(i->second, level + 1);
        }
    } else if (node.IsSequence()) {
        for (size_t i = 0; i < node.size(); ++i) {
            SYLAR_LOG_INFO(SYLAR_LOGGER_ROOT()) << std::string(level * 4, ' ')
                << i << " - " << node[i].Type() << " - " << level;
            print_yaml(node[i], level + 1);
        }
    }
}


void test_yaml() 
{
    YAML::Node root = YAML::LoadFile("/home/greenhandzpx/CodeField/codeCpp/CppRepository/Sylar/config/log.yaml");
    print_yaml(root, 0);
}


namespace sylar {
    template<>
    class LexicalCast<std::string, Person> {
    public:
        Person operator() (const std::string& v)
        {
            // 先把string中的内容加载到node中，在把node中每个节点的内容转成string后再转成T,然后加载到vec中
            YAML::Node node = YAML::Load(v);
            Person p;
            p.m_name = node["name"].as<std::string>();
            p.m_age = node["age"].as<int>();
            p.m_sex = node["sex"].as<bool>();
            return p;
        }
    };

// 特例化从Person转为string
    template<>
    class LexicalCast<Person, std::string> {
    public:
        std::string operator() (const Person& p) {
            YAML::Node node;
            node["name"] = p.m_name;
            node["age"] = p.m_age;
            node["sex"] = p.m_sex;
            std::stringstream ss;
            ss << node;
            return ss.str();
        }
    };
}
void test_config()
{
    //YAML::Node root = YAML::LoadFile
    // SYLAR_LOGGER_ROOT()会返回一个loggermanager中含有的root_logger,已初始化添加了一个stdoutAppender
    std::cout << "Before: \n";
    SYLAR_LOG_INFO(SYLAR_LOGGER_ROOT()) << port_config_var->toString();
    SYLAR_LOG_INFO(SYLAR_LOGGER_ROOT()) << value_config_var->toString();
    SYLAR_LOG_INFO(SYLAR_LOGGER_ROOT()) << repeat_config_var->toString();
    SYLAR_LOG_INFO(SYLAR_LOGGER_ROOT()) << one_person.toString();
    std::vector<int> save_vec = int_vec_config_var->getValue();
    for (size_t i = 0; i < save_vec.size(); ++i) {
        SYLAR_LOG_INFO(SYLAR_LOGGER_ROOT()) << i << " : " << save_vec[i];
    }
    auto save_lis = int_list_config_var->getValue();
    for (auto& i: save_lis) {
        SYLAR_LOG_INFO(SYLAR_LOGGER_ROOT()) << i;
    }


    std::cout << "After: \n";
    YAML::Node root = YAML::LoadFile("/home/greenhandzpx/CodeField/codeCpp/CppRepository/Sylar/config/log.yaml");
    sylar::Config::LoadFromYaml(root);
    SYLAR_LOG_INFO(SYLAR_LOGGER_ROOT()) << port_config_var->toString();
    SYLAR_LOG_INFO(SYLAR_LOGGER_ROOT()) << value_config_var->toString();
    SYLAR_LOG_INFO(SYLAR_LOGGER_ROOT()) << person_config_var->getValue().toString();
    save_vec = int_vec_config_var->getValue();
    for (size_t i = 0; i < save_vec.size(); ++i) {
        SYLAR_LOG_INFO(SYLAR_LOGGER_ROOT()) << i << " : " << save_vec[i];
    }
    save_lis = int_list_config_var->getValue();
    for (auto& i: save_lis) {
        SYLAR_LOG_INFO(SYLAR_LOGGER_ROOT()) << i;
    }
}

void test_callback()
{
    person_config_var->addListener([](const Person& old_one, const Person& new_one){
        SYLAR_LOG_INFO(SYLAR_LOGGER_ROOT()) << "Before: " << old_one.toString();
        SYLAR_LOG_INFO(SYLAR_LOGGER_ROOT()) << "After: " << new_one.toString();
    });

    YAML::Node root = YAML::LoadFile("/home/greenhandzpx/CodeField/codeCpp/CppRepository/Sylar/config/log.yaml");
    sylar::Config::LoadFromYaml(root);
}

void test_logger_config()
{
    std::cout << "\n";
    std::cout << "Before: \n";
    std::cout << sylar::LoggerMgr::GetInstance()->toYamlString() << "\n";
    static sylar::Logger::ptr root_log = SYLAR_LOG_NAME("root");
    SYLAR_LOG_ERROR(root_log) << "First let me try the old one." << std::endl;
    YAML::Node root = YAML::LoadFile("/home/greenhandzpx/CodeField/codeCpp/CppRepository/Sylar/config/logger.yaml");
    sylar::Config::LoadFromYaml(root);
    std::cout << "After: \n";
    static sylar::Logger::ptr system_log = SYLAR_LOG_NAME("system");
    SYLAR_LOG_FATAL(system_log) << "Then let me try the new system one." << std::endl;
    std::cout << sylar::LoggerMgr::GetInstance()->toYamlString() << "\n";
}
int main(int argc, char *argv[])
{

    //test_config();
    //test_yaml();
    test_logger_config();
    //test_callback();
    return 0;
}