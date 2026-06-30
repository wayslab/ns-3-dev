/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "genai-helper.h"
#include "genai-message-header.h"
#include "lognormalmixture/lognormal-mixture-random-variable.h"

#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"

#define BOOST_BIND_GLOBAL_PLACEHOLDERS
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace ns3;
namespace pt = boost::property_tree;

NS_LOG_COMPONENT_DEFINE("GenAISimulation");

namespace
{

struct SimulationConfig
{
    uint32_t seed;
    uint64_t run;
    double durationSeconds;
    double serverStartSeconds;
    double userStartSeconds;
    uint16_t port;
    bool verbose;
    std::string pcapDirectory;
    std::string dataRate;
    std::string channelDelay;
    uint32_t mtuBytes;
    uint32_t tcpSegmentSizeBytes;
    std::string userModality;
    std::string serverModality;
    Ptr<RandomVariableStream> requestSize;
    Ptr<RandomVariableStream> processingDelay;
    Ptr<RandomVariableStream> responseSize;
};

std::string
Lowercase(std::string value)
{
    std::transform(value.begin(),
                   value.end(),
                   value.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return value;
}

Ptr<RandomVariableStream>
LoadDistribution(const pt::ptree& root,
                 const std::string& path,
                 double minimumAllowed,
                 int64_t stream)
{
    const pt::ptree& distribution = root.get_child(path);
    std::string type = Lowercase(distribution.get<std::string>("distribution"));

    if (type == "gaussian" || type == "normal")
    {
        double mean = distribution.get<double>("mean");
        double standardDeviation = distribution.get<double>("standard_deviation");
        double bound = distribution.get<double>("bound");

        if (!std::isfinite(mean) || !std::isfinite(standardDeviation) ||
            !std::isfinite(bound) || standardDeviation < 0.0 || bound < 0.0)
        {
            throw std::runtime_error(path + " contains invalid Gaussian parameters");
        }
        if (mean - bound < minimumAllowed)
        {
            throw std::runtime_error(path + " can sample below its permitted minimum");
        }

        Ptr<NormalRandomVariable> randomVariable = CreateObject<NormalRandomVariable>();
        randomVariable->SetAttribute("Mean", DoubleValue(mean));
        randomVariable->SetAttribute("Variance",
                                     DoubleValue(standardDeviation * standardDeviation));
        randomVariable->SetAttribute("Bound", DoubleValue(bound));
        randomVariable->SetStream(stream);
        return randomVariable;
    }

    if (type == "pareto")
    {
        double scale = distribution.get<double>("x0");
        double shape = distribution.get<double>("alpha");

        if (!std::isfinite(scale) || scale < minimumAllowed || !std::isfinite(shape) ||
            shape <= 0.0)
        {
            throw std::runtime_error(path + " contains invalid Pareto parameters");
        }

        Ptr<ParetoRandomVariable> randomVariable = CreateObject<ParetoRandomVariable>();
        randomVariable->SetAttribute("Scale", DoubleValue(scale));
        randomVariable->SetAttribute("Shape", DoubleValue(shape));
        randomVariable->SetStream(stream);
        return randomVariable;
    }

    if (type == "lognormal" || type == "log-normal")
    {
        double mu = distribution.get<double>("mu");
        double sigma = distribution.get<double>("sigma");

        if (!std::isfinite(mu) || !std::isfinite(sigma) || sigma < 0.0)
        {
            throw std::runtime_error(path + " contains invalid log-normal parameters");
        }

        Ptr<LogNormalRandomVariable> randomVariable =
            CreateObject<LogNormalRandomVariable>();
        randomVariable->SetAttribute("Mu", DoubleValue(mu));
        randomVariable->SetAttribute("Sigma", DoubleValue(sigma));
        randomVariable->SetStream(stream);
        return randomVariable;
    }

    if (type == "lognormal_mixture" || type == "log-normal-mixture")
    {
        Ptr<LogNormalMixtureRandomVariable> randomVariable =
            CreateObject<LogNormalMixtureRandomVariable>();
        double totalWeight = 0.0;

        for (const auto& entry : distribution.get_child("components"))
        {
            const pt::ptree& component = entry.second;
            double weight = component.get<double>("weight");
            double mu = component.get<double>("mu");
            double sigma = component.get<double>("sigma");

            if (!std::isfinite(weight) || weight <= 0.0 || !std::isfinite(mu) ||
                !std::isfinite(sigma) || sigma < 0.0)
            {
                throw std::runtime_error(path + " contains an invalid mixture component");
            }

            randomVariable->AddComponent(weight, mu, sigma);
            totalWeight += weight;
        }

        if (randomVariable->GetComponentCount() == 0 || !std::isfinite(totalWeight))
        {
            throw std::runtime_error(path + " must contain mixture components");
        }

        randomVariable->SetStream(stream);
        return randomVariable;
    }

    throw std::runtime_error(path + ".distribution is unsupported: " + type);
}

SimulationConfig
LoadConfig(const std::string& filename)
{
    pt::ptree root;
    pt::read_json(filename, root);

    SimulationConfig config;
    config.seed = root.get<uint32_t>("simulation.seed");
    config.run = root.get<uint64_t>("simulation.run");
    config.durationSeconds = root.get<double>("simulation.duration_seconds");
    config.serverStartSeconds = root.get<double>("simulation.server_start_seconds");
    config.userStartSeconds = root.get<double>("simulation.user_start_seconds");
    config.port = root.get<uint16_t>("simulation.tcp_port");
    config.verbose = root.get<bool>("simulation.verbose");
    config.pcapDirectory = root.get<std::string>("simulation.pcap_directory");

    config.dataRate = root.get<std::string>("network.data_rate");
    config.channelDelay = root.get<std::string>("network.channel_delay");
    config.mtuBytes = root.get<uint32_t>("network.mtu_bytes");
    config.tcpSegmentSizeBytes = root.get<uint32_t>("network.tcp_segment_size_bytes");

    config.userModality = Lowercase(root.get<std::string>("genai_user.modality"));
    config.serverModality = Lowercase(root.get<std::string>("genai_server.modality"));
    ParseGenAIModality(config.userModality);
    ParseGenAIModality(config.serverModality);

    if (config.durationSeconds <= 0.0 || config.serverStartSeconds < 0.0 ||
        config.userStartSeconds <= config.serverStartSeconds ||
        config.userStartSeconds >= config.durationSeconds)
    {
        throw std::runtime_error("simulation start and duration values are inconsistent");
    }
    if (config.mtuBytes <= 40 || config.tcpSegmentSizeBytes > config.mtuBytes - 52)
    {
        throw std::runtime_error(
            "TCP segment size plus IPv4/TCP timestamp headers must fit inside the MTU");
    }

    RngSeedManager::SetSeed(config.seed);
    RngSeedManager::SetRun(config.run);
    config.requestSize = LoadDistribution(
        root, "genai_user.request_size_bytes." + config.userModality, 1.0, 0);
    std::string modalityPair = config.userModality + "_to_" + config.serverModality;
    config.processingDelay = LoadDistribution(
        root, "genai_server.processing_delay_seconds." + modalityPair, 0.0, 1);
    config.responseSize = LoadDistribution(
        root, "genai_server.response_size_bytes." + modalityPair, 1.0, 2);

    return config;
}

} // namespace

int
main(int argc, char* argv[])
{
    std::string configFile = "rpi_sim/config/text-to-image.json";

    CommandLine cmd(__FILE__);
    cmd.AddValue("config", "Path to the GenAI simulation JSON configuration", configFile);
    cmd.Parse(argc, argv);

    SimulationConfig config;
    try
    {
        config = LoadConfig(configFile);
    }
    catch (const std::exception& error)
    {
        std::cerr << "Failed to load " << configFile << ": " << error.what() << std::endl;
        return 1;
    }

    if (config.verbose)
    {
        LogComponentEnable("GenAIUser", LOG_LEVEL_INFO);
        LogComponentEnable("GenAIServer", LOG_LEVEL_INFO);
    }

    Config::SetDefault("ns3::TcpSocket::SegmentSize",
                       UintegerValue(config.tcpSegmentSizeBytes));

    NodeContainer nodes;
    nodes.Create(2);

    CsmaHelper ethernet;
    ethernet.SetChannelAttribute("DataRate", StringValue(config.dataRate));
    ethernet.SetChannelAttribute("Delay", StringValue(config.channelDelay));
    ethernet.SetDeviceAttribute("Mtu", UintegerValue(config.mtuBytes));
    NetDeviceContainer devices = ethernet.Install(nodes);

    InternetStackHelper internet;
    internet.SetIpv6StackInstall(false);
    internet.Install(nodes);

    NetDeviceContainer userDevice;
    userDevice.Add(devices.Get(0));
    Ipv4AddressHelper userIpv4;
    userIpv4.SetBase("10.0.0.0", "255.255.255.0", "0.0.0.2");
    Ipv4InterfaceContainer userInterface = userIpv4.Assign(userDevice);

    NetDeviceContainer serverDevice;
    serverDevice.Add(devices.Get(1));
    Ipv4AddressHelper serverIpv4;
    serverIpv4.SetBase("10.0.0.0", "255.255.255.0", "0.0.0.200");
    Ipv4InterfaceContainer serverInterface = serverIpv4.Assign(serverDevice);

    GenAIServerHelper server(config.port);
    server.SetAttribute("Modality", StringValue(config.serverModality));
    server.SetAttribute("ProcessingDelay", PointerValue(config.processingDelay));
    server.SetAttribute("ResponseSize", PointerValue(config.responseSize));
    ApplicationContainer serverApp = server.Install(nodes.Get(1));
    serverApp.Start(Seconds(config.serverStartSeconds));
    serverApp.Stop(Seconds(config.durationSeconds));

    GenAIUserHelper user(serverInterface.GetAddress(0), config.port);
    user.SetAttribute("Modality", StringValue(config.userModality));
    user.SetAttribute("RequestSize", PointerValue(config.requestSize));
    ApplicationContainer userApp = user.Install(nodes.Get(0));
    userApp.Start(Seconds(config.userStartSeconds));
    userApp.Stop(Seconds(config.durationSeconds));

    std::filesystem::create_directories(config.pcapDirectory);
    std::string pcapPrefix = config.pcapDirectory + "/" + config.userModality + "_" +
                             config.serverModality + "_pcap";
    ethernet.EnablePcapAll(pcapPrefix);

    std::cout << "Configuration: " << configFile << std::endl;
    std::cout << "PCAP prefix: " << pcapPrefix << std::endl;

    Simulator::Stop(Seconds(config.durationSeconds));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
