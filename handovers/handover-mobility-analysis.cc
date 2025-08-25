#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/lte-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/applications-module.h"
#include "ns3/config-store-module.h"
#include "ns3/flow-monitor-module.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstdlib>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("HandoverMobilityAnalysis");

// Global file streams for data collection
static std::ofstream g_measCsv("handover_meas_reports.csv");
static std::ofstream g_enbRrcCsv("handover_enb_rrc_events.csv");
static std::ofstream g_ueRrcCsv("handover_ue_rrc_events.csv");
static std::ofstream g_mobilityTraceFile("ue_mobility_trace.csv");
static std::ofstream g_throughputFile("throughput_analysis.csv");
static std::ofstream g_handoverStatsFile("handover_statistics.csv");
static std::ofstream g_rsrpFile("rsrp_measurements.csv");

// Global counters for statistics
static uint32_t g_totalHandovers = 0;
static uint32_t g_successfulHandovers = 0;
static uint32_t g_failedHandovers = 0;
static std::map<uint64_t, uint32_t> g_ueHandoverCount;
static std::map<uint64_t, Vector> g_lastUePosition;

// Callback functions for comprehensive data collection
static void
MeasReportSink(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti, LteRrcSap::MeasurementReport report)
{
  const auto& mr = report.measResults;
  uint8_t measId = mr.measId;
  
  // Serving cell measurements
  int srp = mr.measResultPCell.rsrpResult;   // 0..97 quantized per 36.331
  int srq = mr.measResultPCell.rsrqResult;   // 0..34  quantized per 36.331
  
  // Convert quantized values to actual dBm/dB values for analysis
  double rsrpDbm = -140.0 + srp;  // RSRP in dBm
  double rsrqDb = -19.5 + 0.5 * srq;  // RSRQ in dB
  
  bool hasNeigh = mr.haveMeasResultNeighCells;
  std::string event = hasNeigh ? "A3" : "PERIODIC";
  
  g_measCsv << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << ","
            << imsi << ","
            << cellId << ","
            << rnti << ","
            << unsigned(measId) << ","
            << event << ","
            << srp << ","
            << srq << ","
            << rsrpDbm << ","
            << rsrqDb;
  
  // Add neighbor cell measurements if available
  if (hasNeigh)
  {
    g_measCsv << ",";
    for (auto it = mr.measResultListEutra.begin(); it != mr.measResultListEutra.end(); ++it)
    {
      uint16_t neighCellId = it->physCellId;
      int neighRsrp = it->haveRsrpResult ? it->rsrpResult : -1;
      int neighRsrq = it->haveRsrqResult ? it->rsrqResult : -1;
      double neighRsrpDbm = (neighRsrp >= 0) ? (-140.0 + neighRsrp) : -200.0;
      double neighRsrqDb = (neighRsrq >= 0) ? (-19.5 + 0.5 * neighRsrq) : -50.0;
      
      g_measCsv << neighCellId << ":" << neighRsrp << ":" << neighRsrq 
                << ":" << neighRsrpDbm << ":" << neighRsrqDb << ";";
    }
  }
  else
  {
    g_measCsv << ",NONE";
  }
  g_measCsv << std::endl;
  
  // Also log to RSRP file for detailed analysis
  g_rsrpFile << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << ","
             << imsi << "," << cellId << "," << rsrpDbm << "," << rsrqDb << std::endl;
}

static void EnbConnEstablished(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
  g_enbRrcCsv << "CONN_EST," << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << ","
              << imsi << "," << cellId << "," << rnti << std::endl;
}

static void EnbHoStart(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti, uint16_t targetCid)
{
  g_totalHandovers++;
  g_ueHandoverCount[imsi]++;
  
  g_enbRrcCsv << "HO_START," << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << ","
              << imsi << "," << cellId << "," << rnti << ",to:" << targetCid << std::endl;
  
  g_handoverStatsFile << "HO_START," << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << ","
                      << imsi << "," << cellId << "," << targetCid << std::endl;
}

static void EnbHoEndOk(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
  g_successfulHandovers++;
  
  g_enbRrcCsv << "HO_END_OK," << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << ","
              << imsi << "," << cellId << "," << rnti << std::endl;
  
  g_handoverStatsFile << "HO_END_OK," << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << ","
                      << imsi << "," << cellId << std::endl;
}

static void UeConnEstablished(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
  g_ueRrcCsv << "UE_CONN_EST," << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << ","
             << imsi << "," << cellId << "," << rnti << std::endl;
}

static void UeHoStart(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti, uint16_t targetCid)
{
  g_ueRrcCsv << "UE_HO_START," << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << ","
             << imsi << "," << cellId << "," << rnti << ",to:" << targetCid << std::endl;
}

static void UeHoEndOk(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
  g_ueRrcCsv << "UE_HO_END_OK," << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << ","
             << imsi << "," << cellId << "," << rnti << std::endl;
}

// Mobility tracing function
void CourseChange(std::string context, Ptr<const MobilityModel> model)
{
  Vector pos = model->GetPosition();
  Vector vel = model->GetVelocity();
  
  // Extract UE index from context
  std::string::size_type pos1 = context.find("/NodeList/");
  std::string::size_type pos2 = context.find("/", pos1 + 10);
  std::string nodeIdStr = context.substr(pos1 + 10, pos2 - pos1 - 10);
  uint32_t nodeId = std::stoi(nodeIdStr);
  
  g_mobilityTraceFile << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << ","
                      << nodeId << ","
                      << pos.x << "," << pos.y << "," << pos.z << ","
                      << vel.x << "," << vel.y << "," << vel.z << std::endl;
}

// Throughput monitoring function
void MonitorThroughput(Ptr<FlowMonitor> monitor, FlowMonitorHelper* flowHelper)
{
  monitor->CheckForLostPackets();
  std::map<FlowId, FlowMonitor::FlowStats> flowStats = monitor->GetFlowStats();
  
  for (auto iter = flowStats.begin(); iter != flowStats.end(); ++iter)
  {
    FlowId flowId = iter->first;
    FlowMonitor::FlowStats stats = iter->second;
    
    double throughput = stats.rxBytes * 8.0 / (stats.timeLastRxPacket.GetSeconds() - stats.timeFirstTxPacket.GetSeconds()) / 1024 / 1024;
    double delay = stats.delaySum.GetSeconds() / stats.rxPackets;
    double jitter = stats.jitterSum.GetSeconds() / (stats.rxPackets - 1);
    double packetLoss = (double)(stats.txPackets - stats.rxPackets) / stats.txPackets * 100;
    
    g_throughputFile << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << ","
                     << flowId << ","
                     << throughput << ","
                     << delay * 1000 << ","  // delay in ms
                     << jitter * 1000 << ","  // jitter in ms
                     << packetLoss << ","
                     << stats.rxPackets << ","
                     << stats.txPackets << std::endl;
  }
  
  // Schedule next monitoring
  Simulator::Schedule(Seconds(1.0), &MonitorThroughput, monitor, flowHelper);
}

// Function to print final statistics
void PrintFinalStatistics()
{
  std::cout << "\n========== HANDOVER SIMULATION STATISTICS ==========\n";
  std::cout << "Total Handovers Attempted: " << g_totalHandovers << std::endl;
  std::cout << "Successful Handovers: " << g_successfulHandovers << std::endl;
  std::cout << "Failed Handovers: " << g_failedHandovers << std::endl;
  
  if (g_totalHandovers > 0)
  {
    double successRate = (double)g_successfulHandovers / g_totalHandovers * 100;
    std::cout << "Handover Success Rate: " << std::fixed << std::setprecision(2) << successRate << "%" << std::endl;
  }
  
  std::cout << "\nPer-UE Handover Count:\n";
  for (auto& pair : g_ueHandoverCount)
  {
    std::cout << "UE IMSI " << pair.first << ": " << pair.second << " handovers" << std::endl;
  }
  
  std::cout << "\nGenerated Files:\n";
  std::cout << "- handover_meas_reports.csv (measurement reports)\n";
  std::cout << "- handover_enb_rrc_events.csv (eNB RRC events)\n";
  std::cout << "- handover_ue_rrc_events.csv (UE RRC events)\n";
  std::cout << "- ue_mobility_trace.csv (UE positions and velocities)\n";
  std::cout << "- throughput_analysis.csv (throughput and QoS metrics)\n";
  std::cout << "- handover_statistics.csv (handover timing analysis)\n";
  std::cout << "- rsrp_measurements.csv (detailed RSRP/RSRQ data)\n";
  std::cout << "- PCAP files (*.pcap for packet capture analysis)\n";
  std::cout << "===================================================\n";
}

int main(int argc, char* argv[])
{
  // Enhanced simulation parameters
  Time simTime = Seconds(60.0);  // 1 minute simulation
  uint32_t numUes = 4;           // Multiple UEs for better analysis
  uint32_t numEnbs = 5;          // 5 eNBs for more handover opportunities
  bool enableLogs = false;
  bool enablePcap = true;
  double ueSpeed = 15.0;         // 15 m/s (54 km/h) realistic vehicle speed
  
  CommandLine cmd;
  cmd.AddValue("simTime", "Simulation time (s)", simTime);
  cmd.AddValue("numUes", "Number of UEs", numUes);
  cmd.AddValue("numEnbs", "Number of eNBs", numEnbs);
  cmd.AddValue("enableLogs", "Turn on LTE logging", enableLogs);
  cmd.AddValue("enablePcap", "Enable PCAP tracing", enablePcap);
  cmd.AddValue("ueSpeed", "UE speed in m/s", ueSpeed);
  cmd.Parse(argc, argv);

  // Enable logging if requested
  if (enableLogs)
  {
    LogComponentEnable("LteHelper", LOG_LEVEL_INFO);
    LogComponentEnable("LteEnbRrc", LOG_LEVEL_INFO);
    LogComponentEnable("LteUeRrc", LOG_LEVEL_INFO);
    LogComponentEnable("A3RsrpHandoverAlgorithm", LOG_LEVEL_INFO);
  }

  // Configure global random variables
  RngSeedManager::SetSeed(1);
  RngSeedManager::SetRun(1);

  // Create EPC and LTE helpers
  Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
  Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
  lteHelper->SetEpcHelper(epcHelper);
  
  // Enhanced antenna model for better coverage
  lteHelper->SetEnbAntennaModelType("ns3::IsotropicAntennaModel");
  lteHelper->SetUeAntennaModelType("ns3::IsotropicAntennaModel");

  // Create remote host for traffic generation
  Ptr<Node> pgw = epcHelper->GetPgwNode();
  NodeContainer remoteHostContainer;
  remoteHostContainer.Create(1);
  Ptr<Node> remoteHost = remoteHostContainer.Get(0);
  InternetStackHelper internet;
  internet.Install(remoteHostContainer);

  // Create point-to-point link between PGW and remote host
  PointToPointHelper p2ph;
  p2ph.SetDeviceAttribute("DataRate", DataRateValue(DataRate("100Gbps")));
  p2ph.SetChannelAttribute("Delay", TimeValue(MilliSeconds(1)));
  NetDeviceContainer internetDevices = p2ph.Install(pgw, remoteHost);
  
  Ipv4AddressHelper ipv4h;
  ipv4h.SetBase("1.0.0.0", "255.0.0.0");
  Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign(internetDevices);
  
  // Set up routing
  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  Ptr<Ipv4StaticRouting> remoteHostStaticRouting = ipv4RoutingHelper.GetStaticRouting(remoteHost->GetObject<Ipv4>());
  remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

  // Create nodes
  NodeContainer enbNodes;
  enbNodes.Create(numEnbs);
  NodeContainer ueNodes;
  ueNodes.Create(numUes);

  // Position eNBs in a line with 300m spacing for realistic handover scenarios
  MobilityHelper enbMobility;
  Ptr<ListPositionAllocator> enbPositionAlloc = CreateObject<ListPositionAllocator>();
  for (uint32_t i = 0; i < numEnbs; ++i)
  {
    enbPositionAlloc->Add(Vector(i * 300.0, 0.0, 30.0));  // 30m height for eNBs
  }
  enbMobility.SetPositionAllocator(enbPositionAlloc);
  enbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  enbMobility.Install(enbNodes);

  // Set up UE mobility with different patterns
  MobilityHelper ueMobility;
  
  for (uint32_t i = 0; i < numUes; ++i)
  {
    if (i % 2 == 0)
    {
      // Linear movement for even-indexed UEs
      ueMobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
      ueMobility.Install(ueNodes.Get(i));
      
      Ptr<MobilityModel> mobilityModel = ueNodes.Get(i)->GetObject<MobilityModel>();
      Ptr<ConstantVelocityMobilityModel> cvMobility = DynamicCast<ConstantVelocityMobilityModel>(mobilityModel);
      
      // Start position: before first eNB
      cvMobility->SetPosition(Vector(-200.0 + i * 50.0, i * 20.0 - 30.0, 1.5));
      // Move along the eNB line
      cvMobility->SetVelocity(Vector(ueSpeed, 0.0, 0.0));
    }
    else
    {
      // Simple waypoint mobility for odd-indexed UEs
      // Use constant velocity but change direction periodically
      ueMobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
      ueMobility.Install(ueNodes.Get(i));
      
      Ptr<MobilityModel> mobilityModel = ueNodes.Get(i)->GetObject<MobilityModel>();
      Ptr<ConstantVelocityMobilityModel> cvMobility = DynamicCast<ConstantVelocityMobilityModel>(mobilityModel);
      
      // Random starting position within the coverage area
      double startX = -50.0 + (double)(rand() % (int)((numEnbs-1)*300.0 + 100.0));
      double startY = -50.0 + (double)(rand() % 100);
      cvMobility->SetPosition(Vector(startX, startY, 1.5));
      
      // Random velocity direction
      double speed = 5.0 + (double)(rand() % 20); // 5-25 m/s
      double angle = (double)(rand() % 360) * M_PI / 180.0;
      cvMobility->SetVelocity(Vector(speed * cos(angle), speed * sin(angle), 0.0));
    }
  }

  // Install LTE devices
  NetDeviceContainer enbLteDevs = lteHelper->InstallEnbDevice(enbNodes);
  NetDeviceContainer ueLteDevs = lteHelper->InstallUeDevice(ueNodes);

  // Install IP stack on UEs
  internet.Install(ueNodes);
  Ipv4InterfaceContainer ueIpIfaces = epcHelper->AssignUeIpv4Address(NetDeviceContainer(ueLteDevs));

  // Set up default routes for UEs
  for (uint32_t u = 0; u < ueNodes.GetN(); ++u)
  {
    Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting(ueNodes.Get(u)->GetObject<Ipv4>());
    ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
  }

  // Create X2 interfaces between all eNBs for handover support
  for (uint32_t i = 0; i < numEnbs; ++i)
  {
    for (uint32_t j = i + 1; j < numEnbs; ++j)
    {
      lteHelper->AddX2Interface(enbNodes.Get(i), enbNodes.Get(j));
    }
  }

  // Configure handover algorithm with optimized parameters
  lteHelper->SetHandoverAlgorithmType("ns3::A3RsrpHandoverAlgorithm");
  lteHelper->SetHandoverAlgorithmAttribute("Hysteresis", DoubleValue(1.5));  // dB
  lteHelper->SetHandoverAlgorithmAttribute("TimeToTrigger", TimeValue(MilliSeconds(100)));

  // Attach UEs to the first eNB initially
  for (uint32_t i = 0; i < numUes; ++i)
  {
    lteHelper->Attach(ueLteDevs.Get(i), enbLteDevs.Get(0));
  }

  // Set up traffic applications
  ApplicationContainer serverApps, clientApps;
  
  // Downlink traffic: UDP and TCP mixed
  for (uint32_t i = 0; i < numUes; ++i)
  {
    if (i % 2 == 0)
    {
      // UDP traffic for even UEs
      uint16_t dlPort = 1234 + i;
      UdpServerHelper dlPacketSinkHelper(dlPort);
      serverApps.Add(dlPacketSinkHelper.Install(ueNodes.Get(i)));
      
      UdpClientHelper dlClient(ueIpIfaces.GetAddress(i), dlPort);
      dlClient.SetAttribute("Interval", TimeValue(MilliSeconds(10)));  // High frequency
      dlClient.SetAttribute("MaxPackets", UintegerValue(100000));
      dlClient.SetAttribute("PacketSize", UintegerValue(1024));
      clientApps.Add(dlClient.Install(remoteHost));
    }
    else
    {
      // TCP traffic for odd UEs  
      uint16_t dlPort = 1234 + i;
      PacketSinkHelper dlPacketSinkHelper("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), dlPort));
      serverApps.Add(dlPacketSinkHelper.Install(ueNodes.Get(i)));
      
      BulkSendHelper dlClient("ns3::TcpSocketFactory", InetSocketAddress(ueIpIfaces.GetAddress(i), dlPort));
      dlClient.SetAttribute("MaxBytes", UintegerValue(0));  // Unlimited
      dlClient.SetAttribute("SendSize", UintegerValue(1460));
      clientApps.Add(dlClient.Install(remoteHost));
    }
  }

  // Uplink traffic for some UEs
  for (uint32_t i = 0; i < numUes/2; ++i)
  {
    uint16_t ulPort = 2000 + i;
    UdpServerHelper ulPacketSinkHelper(ulPort);
    serverApps.Add(ulPacketSinkHelper.Install(remoteHost));
    
    UdpClientHelper ulClient(internetIpIfaces.GetAddress(1), ulPort);
    ulClient.SetAttribute("Interval", TimeValue(MilliSeconds(50)));
    ulClient.SetAttribute("MaxPackets", UintegerValue(50000));
    ulClient.SetAttribute("PacketSize", UintegerValue(512));
    clientApps.Add(ulClient.Install(ueNodes.Get(i)));
  }

  // Start applications
  serverApps.Start(Seconds(0.5));
  clientApps.Start(Seconds(1.0));

  // Set up flow monitoring for throughput analysis
  FlowMonitorHelper flowHelper;
  Ptr<FlowMonitor> monitor = flowHelper.InstallAll();
  
  // Schedule throughput monitoring
  Simulator::Schedule(Seconds(2.0), &MonitorThroughput, monitor, &flowHelper);

  // Enable PCAP tracing if requested
  if (enablePcap)
  {
    lteHelper->EnablePdcpTraces();
    lteHelper->EnableRlcTraces();
    p2ph.EnablePcapAll("handover-analysis");
  }

  // Connect all trace sources for comprehensive data collection
  Config::Connect("/NodeList/*/DeviceList/*/LteEnbRrc/RecvMeasurementReport",
                  MakeCallback(&MeasReportSink));
  
  Config::Connect("/NodeList/*/DeviceList/*/LteEnbRrc/ConnectionEstablished",
                  MakeCallback(&EnbConnEstablished));
  Config::Connect("/NodeList/*/DeviceList/*/LteEnbRrc/HandoverStart",
                  MakeCallback(&EnbHoStart));
  Config::Connect("/NodeList/*/DeviceList/*/LteEnbRrc/HandoverEndOk",
                  MakeCallback(&EnbHoEndOk));

  Config::Connect("/NodeList/*/DeviceList/*/LteUeRrc/ConnectionEstablished",
                  MakeCallback(&UeConnEstablished));
  Config::Connect("/NodeList/*/DeviceList/*/LteUeRrc/HandoverStart",
                  MakeCallback(&UeHoStart));
  Config::Connect("/NodeList/*/DeviceList/*/LteUeRrc/HandoverEndOk",
                  MakeCallback(&UeHoEndOk));

  // Connect mobility tracing
  Config::Connect("/NodeList/*/$ns3::MobilityModel/CourseChange",
                  MakeCallback(&CourseChange));

  // Initialize CSV file headers
  g_measCsv << "time,imsi,enbCellId,rnti,measId,event,servingRsrpQ,servingRsrqQ,servingRsrpDbm,servingRsrqDb,neighborCells\n";
  g_enbRrcCsv << "event,time,imsi,cellId,rnti,info\n";
  g_ueRrcCsv << "event,time,imsi,cellId,rnti,info\n";
  g_mobilityTraceFile << "time,nodeId,posX,posY,posZ,velX,velY,velZ\n";
  g_throughputFile << "time,flowId,throughputMbps,delayMs,jitterMs,packetLossPercent,rxPackets,txPackets\n";
  g_handoverStatsFile << "event,time,imsi,sourceCellId,targetCellId\n";
  g_rsrpFile << "time,imsi,cellId,rsrpDbm,rsrqDb\n";

  std::cout << "Starting handover mobility analysis simulation...\n";
  std::cout << "Simulation parameters:\n";
  std::cout << "- Duration: " << simTime.GetSeconds() << " seconds\n";
  std::cout << "- Number of UEs: " << numUes << "\n";
  std::cout << "- Number of eNBs: " << numEnbs << "\n";
  std::cout << "- UE Speed: " << ueSpeed << " m/s\n";
  std::cout << "- PCAP Tracing: " << (enablePcap ? "Enabled" : "Disabled") << "\n";

  // Run simulation
  Simulator::Stop(simTime);
  Simulator::Run();

  // Final flow monitor check
  monitor->CheckForLostPackets();
  
  // Clean up
  Simulator::Destroy();

  // Close all files
  g_measCsv.close();
  g_enbRrcCsv.close();
  g_ueRrcCsv.close();
  g_mobilityTraceFile.close();
  g_throughputFile.close();
  g_handoverStatsFile.close();
  g_rsrpFile.close();

  // Print final statistics
  PrintFinalStatistics();

  return 0;
}
