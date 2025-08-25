#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/lte-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/applications-module.h"
#include "ns3/config-store-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/netanim-module.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstdlib>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("ComprehensiveHandoverAnalysis");

// Global variables for NetAnim
static AnimationInterface* g_anim = nullptr;
static std::map<uint64_t, uint32_t> g_ueToNodeId; // Map IMSI to node ID for NetAnim

// Global file streams for comprehensive data collection
static std::ofstream g_measCsv("comprehensive_meas_reports.csv");
static std::ofstream g_enbRrcCsv("comprehensive_enb_rrc_events.csv");
static std::ofstream g_ueRrcCsv("comprehensive_ue_rrc_events.csv");
static std::ofstream g_mobilityTraceFile("comprehensive_ue_mobility_trace.csv");
static std::ofstream g_throughputFile("comprehensive_throughput_analysis.csv");
static std::ofstream g_handoverStatsFile("comprehensive_handover_statistics.csv");
static std::ofstream g_rsrpFile("comprehensive_rsrp_measurements.csv");
static std::ofstream g_baseStationFile("comprehensive_base_station_info.csv");
static std::ofstream g_securityEventsFile("comprehensive_security_events.csv");

// Global counters and tracking variables
static uint32_t g_totalHandovers = 0;
static uint32_t g_successfulHandovers = 0;
static uint32_t g_failedHandovers = 0;
static uint32_t g_fakeAttachAttempts = 0;
static uint32_t g_faultyHandovers = 0;
static std::map<uint64_t, uint32_t> g_ueHandoverCount;
static std::map<uint64_t, Vector> g_lastUePosition;
static std::map<uint16_t, std::string> g_baseStationTypes; // cellId -> type (legitimate/faulty/fake)

// Enhanced measurement report callback with base station classification
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
  
  // Determine serving cell type
  std::string servingCellType = "UNKNOWN";
  if (g_baseStationTypes.find(cellId) != g_baseStationTypes.end())
  {
    servingCellType = g_baseStationTypes[cellId];
  }
  
  g_measCsv << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << ","
            << imsi << ","
            << cellId << ","
            << servingCellType << ","
            << rnti << ","
            << unsigned(measId) << ","
            << event << ","
            << srp << ","
            << srq << ","
            << rsrpDbm << ","
            << rsrqDb;
  
  // Add neighbor cell measurements with classification
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
      
      std::string neighType = "UNKNOWN";
      if (g_baseStationTypes.find(neighCellId) != g_baseStationTypes.end())
      {
        neighType = g_baseStationTypes[neighCellId];
      }
      
      g_measCsv << neighCellId << ":" << neighType << ":" << neighRsrp << ":" << neighRsrq 
                << ":" << neighRsrpDbm << ":" << neighRsrqDb << ";";
      
      // Log potential security events
      if (neighType == "FAKE" && neighRsrpDbm > rsrpDbm + 3.0)  // Strong fake signal
      {
        g_securityEventsFile << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << ","
                            << "STRONG_FAKE_SIGNAL,IMSI:" << imsi << ",FakeCellId:" << neighCellId 
                            << ",FakeRSRP:" << neighRsrpDbm << ",ServingRSRP:" << rsrpDbm << std::endl;
      }
    }
  }
  else
  {
    g_measCsv << ",NONE";
  }
  g_measCsv << std::endl;
  
  // Enhanced RSRP file with base station classification
  g_rsrpFile << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << ","
             << imsi << "," << cellId << "," << servingCellType << "," 
             << rsrpDbm << "," << rsrqDb << std::endl;
}

static void EnbConnEstablished(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
  std::string cellType = g_baseStationTypes[cellId];
  g_enbRrcCsv << "CONN_EST," << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << ","
              << imsi << "," << cellId << "," << cellType << "," << rnti << std::endl;
  
  // Update NetAnim visualization for connection establishment
  if (g_anim && g_ueToNodeId.find(imsi) != g_ueToNodeId.end())
  {
    uint32_t ueNodeId = g_ueToNodeId[imsi];
    if (cellType == "LEGITIMATE")
    {
      g_anim->UpdateNodeColor(ueNodeId, 0, 255, 0); // Green when connected to legitimate
    }
    else if (cellType == "FAULTY")
    {
      g_anim->UpdateNodeColor(ueNodeId, 255, 165, 0); // Orange when connected to faulty
    }
    else if (cellType == "FAKE")
    {
      g_anim->UpdateNodeColor(ueNodeId, 255, 0, 255); // Magenta when connected to fake
    }
  }
  
  // Track fake connection attempts
  if (cellType == "FAKE")
  {
    g_fakeAttachAttempts++;
    g_securityEventsFile << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << ","
                        << "FAKE_ATTACH_ATTEMPT,IMSI:" << imsi << ",FakeCellId:" << cellId << std::endl;
  }
}

static void EnbHoStart(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti, uint16_t targetCid)
{
  g_totalHandovers++;
  g_ueHandoverCount[imsi]++;
  
  std::string sourceCellType = g_baseStationTypes[cellId];
  std::string targetCellType = g_baseStationTypes[targetCid];
  
  g_enbRrcCsv << "HO_START," << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << ","
              << imsi << "," << cellId << "," << sourceCellType << "," << rnti 
              << ",to:" << targetCid << "," << targetCellType << std::endl;
  
  g_handoverStatsFile << "HO_START," << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << ","
                      << imsi << "," << cellId << "," << sourceCellType << "," 
                      << targetCid << "," << targetCellType << std::endl;
  
  // Update NetAnim visualization for handover start
  if (g_anim && g_ueToNodeId.find(imsi) != g_ueToNodeId.end())
  {
    uint32_t ueNodeId = g_ueToNodeId[imsi];
    g_anim->UpdateNodeColor(ueNodeId, 255, 255, 0); // Yellow during handover
    g_anim->UpdateNodeDescription(ueNodeId, "UE-" + std::to_string(imsi) + "-HO:" + std::to_string(cellId) + "→" + std::to_string(targetCid));
  }
  
  // Track faulty handovers
  if (sourceCellType == "FAULTY" || targetCellType == "FAULTY")
  {
    g_faultyHandovers++;
    g_securityEventsFile << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << ","
                        << "FAULTY_HANDOVER,IMSI:" << imsi << ",Source:" << cellId << "(" << sourceCellType 
                        << "),Target:" << targetCid << "(" << targetCellType << ")" << std::endl;
  }
  
  // Track fake handover attempts
  if (targetCellType == "FAKE")
  {
    g_securityEventsFile << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << ","
                        << "FAKE_HANDOVER_ATTEMPT,IMSI:" << imsi << ",Source:" << cellId 
                        << ",FakeTarget:" << targetCid << std::endl;
  }
}

static void EnbHoEndOk(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
  g_successfulHandovers++;
  
  std::string cellType = g_baseStationTypes[cellId];
  g_enbRrcCsv << "HO_END_OK," << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << ","
              << imsi << "," << cellId << "," << cellType << "," << rnti << std::endl;
  
  g_handoverStatsFile << "HO_END_OK," << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << ","
                      << imsi << "," << cellId << "," << cellType << std::endl;
  
  // Update NetAnim visualization for successful handover completion
  if (g_anim && g_ueToNodeId.find(imsi) != g_ueToNodeId.end())
  {
    uint32_t ueNodeId = g_ueToNodeId[imsi];
    if (cellType == "LEGITIMATE")
    {
      g_anim->UpdateNodeColor(ueNodeId, 0, 255, 0); // Green when connected to legitimate
    }
    else if (cellType == "FAULTY")
    {
      g_anim->UpdateNodeColor(ueNodeId, 255, 165, 0); // Orange when connected to faulty
    }
    else if (cellType == "FAKE")
    {
      g_anim->UpdateNodeColor(ueNodeId, 255, 0, 255); // Magenta when connected to fake
    }
    g_anim->UpdateNodeDescription(ueNodeId, "UE-" + std::to_string(imsi) + "-Cell:" + std::to_string(cellId));
  }
}

static void UeConnEstablished(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
  std::string cellType = g_baseStationTypes[cellId];
  g_ueRrcCsv << "UE_CONN_EST," << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << ","
             << imsi << "," << cellId << "," << cellType << "," << rnti << std::endl;
}

static void UeHoStart(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti, uint16_t targetCid)
{
  std::string sourceCellType = g_baseStationTypes[cellId];
  std::string targetCellType = g_baseStationTypes[targetCid];
  
  g_ueRrcCsv << "UE_HO_START," << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << ","
             << imsi << "," << cellId << "," << sourceCellType << "," << rnti 
             << ",to:" << targetCid << "," << targetCellType << std::endl;
}

static void UeHoEndOk(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
  std::string cellType = g_baseStationTypes[cellId];
  g_ueRrcCsv << "UE_HO_END_OK," << std::fixed << std::setprecision(6) << Simulator::Now().GetSeconds() << ","
             << imsi << "," << cellId << "," << cellType << "," << rnti << std::endl;
}

// Enhanced mobility tracing function
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
                      << vel.x << "," << vel.y << "," << vel.z << ","
                      << sqrt(vel.x*vel.x + vel.y*vel.y) << std::endl;  // Speed magnitude
}

// Function to change UE direction to ensure interaction with all BS types
void ChangeUeDirection(uint32_t ueIndex, NodeContainer ueNodes, double speed)
{
  Ptr<MobilityModel> mobilityModel = ueNodes.Get(ueIndex)->GetObject<MobilityModel>();
  Ptr<ConstantVelocityMobilityModel> cvMobility = DynamicCast<ConstantVelocityMobilityModel>(mobilityModel);
  
  Vector currentPos = cvMobility->GetPosition();
  
  if (ueIndex == 1)  // UE 1 doing vertical zigzag
  {
    if (currentPos.y > 200.0)
    {
      cvMobility->SetVelocity(Vector(0.0, -speed, 0.0)); // Go down
    }
    else if (currentPos.y < -200.0)
    {
      cvMobility->SetVelocity(Vector(0.0, speed, 0.0));  // Go up
    }
  }
  
  // Schedule next direction change
  Simulator::Schedule(Seconds(20.0), &ChangeUeDirection, ueIndex, ueNodes, speed);
}

// Enhanced throughput monitoring function
void MonitorThroughput(Ptr<FlowMonitor> monitor, FlowMonitorHelper* flowHelper)
{
  monitor->CheckForLostPackets();
  std::map<FlowId, FlowMonitor::FlowStats> flowStats = monitor->GetFlowStats();
  
  for (auto iter = flowStats.begin(); iter != flowStats.end(); ++iter)
  {
    FlowId flowId = iter->first;
    FlowMonitor::FlowStats stats = iter->second;
    
    if (stats.rxPackets > 0)
    {
      double throughput = stats.rxBytes * 8.0 / (stats.timeLastRxPacket.GetSeconds() - stats.timeFirstTxPacket.GetSeconds()) / 1024 / 1024;
      double delay = stats.delaySum.GetSeconds() / stats.rxPackets;
      double jitter = (stats.rxPackets > 1) ? (stats.jitterSum.GetSeconds() / (stats.rxPackets - 1)) : 0.0;
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
  }
  
  // Schedule next monitoring
  Simulator::Schedule(Seconds(1.0), &MonitorThroughput, monitor, flowHelper);
}

// Function to print comprehensive final statistics
void PrintFinalStatistics()
{
  std::cout << "\n========== COMPREHENSIVE HANDOVER SIMULATION STATISTICS ==========\n";
  std::cout << "Total Handovers Attempted: " << g_totalHandovers << std::endl;
  std::cout << "Successful Handovers: " << g_successfulHandovers << std::endl;
  std::cout << "Failed Handovers: " << g_failedHandovers << std::endl;
  std::cout << "Fake Attach Attempts: " << g_fakeAttachAttempts << std::endl;
  std::cout << "Faulty Base Station Handovers: " << g_faultyHandovers << std::endl;
  
  if (g_totalHandovers > 0)
  {
    double successRate = (double)g_successfulHandovers / g_totalHandovers * 100;
    std::cout << "Handover Success Rate: " << std::fixed << std::setprecision(2) << successRate << "%" << std::endl;
  }
  
  std::cout << "\nBase Station Classification:\n";
  for (auto& pair : g_baseStationTypes)
  {
    std::cout << "Cell ID " << pair.first << ": " << pair.second << std::endl;
  }
  
  std::cout << "\nPer-UE Handover Count:\n";
  for (auto& pair : g_ueHandoverCount)
  {
    std::cout << "UE IMSI " << pair.first << ": " << pair.second << " handovers" << std::endl;
  }
  
  std::cout << "\nGenerated Files:\n";
  std::cout << "- comprehensive_meas_reports.csv (measurement reports with BS classification)\n";
  std::cout << "- comprehensive_enb_rrc_events.csv (eNB RRC events)\n";
  std::cout << "- comprehensive_ue_rrc_events.csv (UE RRC events)\n";
  std::cout << "- comprehensive_ue_mobility_trace.csv (UE positions and velocities)\n";
  std::cout << "- comprehensive_throughput_analysis.csv (throughput and QoS metrics)\n";
  std::cout << "- comprehensive_handover_statistics.csv (handover timing analysis)\n";
  std::cout << "- comprehensive_rsrp_measurements.csv (detailed RSRP/RSRQ data)\n";
  std::cout << "- comprehensive_base_station_info.csv (base station classifications)\n";
  std::cout << "- comprehensive_security_events.csv (security-related events)\n";
  std::cout << "- comprehensive-handover-analysis.xml (NetAnim visualization file)\n";
  std::cout << "- PCAP files (*.pcap for packet capture analysis)\n";
  std::cout << "\nTo visualize the simulation:\n";
  std::cout << "1. Open NetAnim application\n";
  std::cout << "2. Load the file: comprehensive-handover-analysis.xml\n";
  std::cout << "3. Click Play to see the network topology and UE movements\n";
  std::cout << "=================================================================\n";
}

int main(int argc, char* argv[])
{
  // Optimized simulation parameters for comprehensive BS interaction
  Time simTime = Seconds(120.0); // Longer time for more interactions
  uint32_t numUes = 3;           // Fewer UEs but more focused mobility
  uint32_t numLegitEnbs = 2;     // Legitimate base stations
  uint32_t numFaultyEnbs = 1;    // Faulty but legitimate base stations  
  uint32_t numFakeEnbs = 1;      // Fake/rogue base stations
  bool enableLogs = false;
  bool enablePcap = false;       // Disable by default to reduce file size
  bool enableNetAnim = true;     // Enable NetAnim visualization by default
  double ueSpeed = 15.0;         // Moderate speed for better interaction
  
  CommandLine cmd;
  cmd.AddValue("simTime", "Simulation time (s)", simTime);
  cmd.AddValue("numUes", "Number of UEs", numUes);
  cmd.AddValue("numLegitEnbs", "Number of legitimate eNBs", numLegitEnbs);
  cmd.AddValue("numFaultyEnbs", "Number of faulty eNBs", numFaultyEnbs);
  cmd.AddValue("numFakeEnbs", "Number of fake eNBs", numFakeEnbs);
  cmd.AddValue("enableLogs", "Turn on LTE logging", enableLogs);
  cmd.AddValue("enablePcap", "Enable PCAP tracing", enablePcap);
  cmd.AddValue("enableNetAnim", "Enable NetAnim visualization", enableNetAnim);
  cmd.AddValue("ueSpeed", "UE speed in m/s", ueSpeed);
  cmd.Parse(argc, argv);

  uint32_t totalEnbs = numLegitEnbs + numFaultyEnbs + numFakeEnbs;

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
  
  // Enhanced antenna model
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

  // Set constant positions for infrastructure nodes (to avoid NetAnim warnings)
  MobilityHelper infraMobility;
  infraMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  infraMobility.Install(pgw);
  infraMobility.Install(remoteHost);
  
  // Position infrastructure nodes
  Ptr<MobilityModel> pgwMobility = pgw->GetObject<MobilityModel>();
  pgwMobility->SetPosition(Vector(-300.0, 0.0, 0.0));
  
  Ptr<MobilityModel> remoteHostMobility = remoteHost->GetObject<MobilityModel>();
  remoteHostMobility->SetPosition(Vector(-400.0, 0.0, 0.0));

  // Create nodes
  NodeContainer enbNodes;
  enbNodes.Create(totalEnbs);
  NodeContainer ueNodes;
  ueNodes.Create(numUes);

  // Position eNBs in a closer pattern to force interactions
  MobilityHelper enbMobility;
  Ptr<ListPositionAllocator> enbPositionAlloc = CreateObject<ListPositionAllocator>();
  
  // Legitimate eNBs - closer spacing for overlap
  for (uint32_t i = 0; i < numLegitEnbs; ++i)
  {
    enbPositionAlloc->Add(Vector(i * 250.0, 0.0, 30.0));  // Reduced spacing
    g_baseStationTypes[i + 1] = "LEGITIMATE";  // Cell IDs start from 1
  }
  
  // Faulty eNB - positioned to create overlap with legitimate
  for (uint32_t i = 0; i < numFaultyEnbs; ++i)
  {
    enbPositionAlloc->Add(Vector(125.0, 150.0, 30.0));  // Strategic position
    g_baseStationTypes[numLegitEnbs + i + 1] = "FAULTY";
  }
  
  // Fake eNB - positioned to intercept UE paths
  for (uint32_t i = 0; i < numFakeEnbs; ++i)
  {
    enbPositionAlloc->Add(Vector(125.0, -150.0, 30.0));  // Strategic interception point
    g_baseStationTypes[numLegitEnbs + numFaultyEnbs + i + 1] = "FAKE";
  }
  
  enbMobility.SetPositionAllocator(enbPositionAlloc);
  enbMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  enbMobility.Install(enbNodes);

  // Set up strategic UE mobility patterns to interact with all BS types
  MobilityHelper ueMobility;
  
  for (uint32_t i = 0; i < numUes; ++i)
  {
    // All UEs use strategic paths to encounter all base station types
    ueMobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    ueMobility.Install(ueNodes.Get(i));
    
    Ptr<MobilityModel> mobilityModel = ueNodes.Get(i)->GetObject<MobilityModel>();
    Ptr<ConstantVelocityMobilityModel> cvMobility = DynamicCast<ConstantVelocityMobilityModel>(mobilityModel);
    
    if (i == 0)
    {
      // UE 0: Horizontal path through all legitimate eNBs and near fake/faulty
      cvMobility->SetPosition(Vector(-200.0, 0.0, 1.5));
      cvMobility->SetVelocity(Vector(ueSpeed, 0.0, 0.0));
    }
    else if (i == 1)
    {
      // UE 1: Vertical zigzag to encounter all types
      cvMobility->SetPosition(Vector(125.0, -300.0, 1.5));
      cvMobility->SetVelocity(Vector(0.0, ueSpeed, 0.0));
    }
    else
    {
      // UE 2: Diagonal path intersecting all coverage areas
      cvMobility->SetPosition(Vector(-100.0, -200.0, 1.5));
      cvMobility->SetVelocity(Vector(ueSpeed * 0.7, ueSpeed * 0.7, 0.0));
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

  // Create X2 interfaces only between legitimate and faulty eNBs (not fake ones)
  for (uint32_t i = 0; i < numLegitEnbs + numFaultyEnbs; ++i)
  {
    for (uint32_t j = i + 1; j < numLegitEnbs + numFaultyEnbs; ++j)
    {
      lteHelper->AddX2Interface(enbNodes.Get(i), enbNodes.Get(j));
    }
  }

  // Configure handover algorithm with more aggressive parameters
  lteHelper->SetHandoverAlgorithmType("ns3::A3RsrpHandoverAlgorithm");
  lteHelper->SetHandoverAlgorithmAttribute("Hysteresis", DoubleValue(1.0));      // Lower hysteresis
  lteHelper->SetHandoverAlgorithmAttribute("TimeToTrigger", TimeValue(MilliSeconds(64))); // Faster trigger

  // Apply specific configurations to different base station types
  for (uint32_t i = 0; i < totalEnbs; ++i)
  {
    uint32_t nodeId = enbNodes.Get(i)->GetId();
    uint16_t cellId = i + 1;
    
    if (g_baseStationTypes[cellId] == "LEGITIMATE")
    {
      // Normal power and parameters for legitimate eNBs
      Config::Set("/NodeList/" + std::to_string(nodeId) + "/DeviceList/*/LteEnbPhy/TxPower", 
                  DoubleValue(43.0)); // 20W
    }
    else if (g_baseStationTypes[cellId] == "FAULTY")
    {
      // Reduced power and poor handover parameters for faulty eNBs
      Config::Set("/NodeList/" + std::to_string(nodeId) + "/DeviceList/*/LteEnbPhy/TxPower", 
                  DoubleValue(25.0)); // Slightly better than before but still poor
      Config::Set("/NodeList/" + std::to_string(nodeId) + "/DeviceList/*/LteEnbRrc/HandoverAlgorithm/Hysteresis", 
                  DoubleValue(6.0));  // Reduced from 8.0
      Config::Set("/NodeList/" + std::to_string(nodeId) + "/DeviceList/*/LteEnbRrc/HandoverAlgorithm/TimeToTrigger", 
                  TimeValue(MilliSeconds(320))); // Reduced from 640ms
    }
    else if (g_baseStationTypes[cellId] == "FAKE")
    {
      // High power to attract UEs but configure as CSG for access denial
      Config::Set("/NodeList/" + std::to_string(nodeId) + "/DeviceList/*/LteEnbPhy/TxPower", 
                  DoubleValue(40.0)); // Reduced from 46.0 to be attractive but not overwhelming
      Config::Set("/NodeList/" + std::to_string(nodeId) + "/DeviceList/*/LteEnbNetDevice/CsgIndication", 
                  BooleanValue(true));
      Config::Set("/NodeList/" + std::to_string(nodeId) + "/DeviceList/*/LteEnbNetDevice/CsgId", 
                  UintegerValue(999)); // Arbitrary CSG ID that UEs won't have
    }
  }

  // Attach UEs to the first legitimate eNB initially
  for (uint32_t i = 0; i < numUes; ++i)
  {
    lteHelper->Attach(ueLteDevs.Get(i), enbLteDevs.Get(0));
  }

  // Set up lightweight traffic applications to reduce PCAP size
  ApplicationContainer serverApps, clientApps;
  
  // Simplified traffic patterns - only essential traffic
  for (uint32_t i = 0; i < numUes; ++i)
  {
    // Light downlink UDP traffic only
    uint16_t dlPort = 1234 + i;
    
    UdpServerHelper dlPacketSinkHelper(dlPort);
    serverApps.Add(dlPacketSinkHelper.Install(ueNodes.Get(i)));
    
    UdpClientHelper dlClient(ueIpIfaces.GetAddress(i), dlPort);
    dlClient.SetAttribute("Interval", TimeValue(MilliSeconds(100)));  // Reduced frequency
    dlClient.SetAttribute("MaxPackets", UintegerValue(10000));        // Fewer packets
    dlClient.SetAttribute("PacketSize", UintegerValue(512));          // Smaller packets
    clientApps.Add(dlClient.Install(remoteHost));
  }

  // Start applications
  serverApps.Start(Seconds(0.5));
  clientApps.Start(Seconds(1.0));

  // Set up flow monitoring
  FlowMonitorHelper flowHelper;
  Ptr<FlowMonitor> monitor = flowHelper.InstallAll();
  
  // Schedule throughput monitoring
  Simulator::Schedule(Seconds(2.0), &MonitorThroughput, monitor, &flowHelper);
  
  // Schedule UE direction changes to ensure interaction with all BS types
  for (uint32_t i = 0; i < numUes; ++i)
  {
    Simulator::Schedule(Seconds(20.0), &ChangeUeDirection, i, ueNodes, ueSpeed);
  }

  // Enable selective PCAP tracing if requested (only control plane)
  if (enablePcap)
  {
    // Only enable RRC traces for handover analysis, not all traffic
    lteHelper->EnableRlcTraces();
    // Skip PDCP traces to reduce file size significantly
    // lteHelper->EnablePdcpTraces();
    
    // Only trace control plane on P2P link, not user data
    p2ph.EnablePcap("comprehensive-handover-control", internetDevices.Get(0), true);
  }

  // Connect all trace sources
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
  g_measCsv << "time,imsi,enbCellId,cellType,rnti,measId,event,servingRsrpQ,servingRsrqQ,servingRsrpDbm,servingRsrqDb,neighborCells\n";
  g_enbRrcCsv << "event,time,imsi,cellId,cellType,rnti,info\n";
  g_ueRrcCsv << "event,time,imsi,cellId,cellType,rnti,info\n";
  g_mobilityTraceFile << "time,nodeId,posX,posY,posZ,velX,velY,velZ,speed\n";
  g_throughputFile << "time,flowId,throughputMbps,delayMs,jitterMs,packetLossPercent,rxPackets,txPackets\n";
  g_handoverStatsFile << "event,time,imsi,sourceCellId,sourceCellType,targetCellId,targetCellType\n";
  g_rsrpFile << "time,imsi,cellId,cellType,rsrpDbm,rsrqDb\n";
  g_baseStationFile << "cellId,nodeId,cellType,posX,posY,posZ,txPowerDbm\n";
  g_securityEventsFile << "time,eventType,details\n";

  // Write base station information
  for (uint32_t i = 0; i < totalEnbs; ++i)
  {
    Vector pos = enbNodes.Get(i)->GetObject<MobilityModel>()->GetPosition();
    uint16_t cellId = i + 1;
    std::string cellType = g_baseStationTypes[cellId];
    
    double txPower = 43.0; // Default
    if (cellType == "FAULTY") txPower = 25.0;        // Updated value
    else if (cellType == "FAKE") txPower = 40.0;     // Updated value
    
    g_baseStationFile << cellId << "," << enbNodes.Get(i)->GetId() << "," 
                      << cellType << "," << pos.x << "," << pos.y << "," 
                      << pos.z << "," << txPower << std::endl;
  }

  std::cout << "Starting comprehensive handover analysis simulation...\n";
  std::cout << "Simulation parameters:\n";
  std::cout << "- Duration: " << simTime.GetSeconds() << " seconds\n";
  std::cout << "- Number of UEs: " << numUes << "\n";
  std::cout << "- Legitimate eNBs: " << numLegitEnbs << "\n";
  std::cout << "- Faulty eNBs: " << numFaultyEnbs << "\n";
  std::cout << "- Fake eNBs: " << numFakeEnbs << "\n";
  std::cout << "- UE Speed: " << ueSpeed << " m/s\n";
  std::cout << "- PCAP Tracing: " << (enablePcap ? "Enabled" : "Disabled") << "\n";
  std::cout << "- NetAnim Visualization: " << (enableNetAnim ? "Enabled" : "Disabled") << "\n";

  // Set up NetAnim visualization
  AnimationInterface* anim = nullptr;
  if (enableNetAnim)
  {
    anim = new AnimationInterface("comprehensive-handover-analysis.xml");
    g_anim = anim; // Set global reference for callbacks
    
    // Create IMSI to Node ID mapping for UEs
    for (uint32_t i = 0; i < numUes; ++i)
    {
      g_ueToNodeId[i + 1] = ueNodes.Get(i)->GetId(); // IMSI starts from 1
    }
    
    // Set node descriptions and colors for better visualization
    for (uint32_t i = 0; i < totalEnbs; ++i)
    {
      uint16_t cellId = i + 1;
      std::string cellType = g_baseStationTypes[cellId];
      
      if (cellType == "LEGITIMATE")
      {
        anim->UpdateNodeDescription(enbNodes.Get(i), "Legit-eNB-" + std::to_string(cellId));
        anim->UpdateNodeColor(enbNodes.Get(i), 0, 255, 0); // Green for legitimate
      }
      else if (cellType == "FAULTY")
      {
        anim->UpdateNodeDescription(enbNodes.Get(i), "Faulty-eNB-" + std::to_string(cellId));
        anim->UpdateNodeColor(enbNodes.Get(i), 255, 165, 0); // Orange for faulty
      }
      else if (cellType == "FAKE")
      {
        anim->UpdateNodeDescription(enbNodes.Get(i), "Fake-eNB-" + std::to_string(cellId));
        anim->UpdateNodeColor(enbNodes.Get(i), 255, 0, 0); // Red for fake/rogue
      }
      
      // Set eNB size and shape
      anim->UpdateNodeSize(enbNodes.Get(i), 15.0, 15.0);
    }
    
    // Configure UE visualization
    for (uint32_t i = 0; i < numUes; ++i)
    {
      anim->UpdateNodeDescription(ueNodes.Get(i), "UE-" + std::to_string(i + 1));
      anim->UpdateNodeColor(ueNodes.Get(i), 0, 0, 255); // Blue for UEs initially
      anim->UpdateNodeSize(ueNodes.Get(i), 8.0, 8.0);
    }
    
    // Configure network infrastructure nodes
    anim->UpdateNodeDescription(pgw, "PGW");
    anim->UpdateNodeColor(pgw, 128, 0, 128); // Purple for PGW
    anim->UpdateNodeSize(pgw, 20.0, 20.0);
    
    anim->UpdateNodeDescription(remoteHost, "RemoteHost");
    anim->UpdateNodeColor(remoteHost, 0, 128, 128); // Teal for remote host
    anim->UpdateNodeSize(remoteHost, 12.0, 12.0);
    
    // Enable packet animation for key flows
    anim->EnablePacketMetadata(true);
    
    // Set update interval for smooth animation
    anim->SetMaxPktsPerTraceFile(50000);
    
    std::cout << "NetAnim XML file will be generated: comprehensive-handover-analysis.xml\n";
    std::cout << "You can open this file with NetAnim to visualize the simulation.\n";
    std::cout << "Color coding: Green=Legitimate eNB, Orange=Faulty eNB, Red=Fake eNB\n";
    std::cout << "UE colors change based on connection: Green=Legitimate, Orange=Faulty, Magenta=Fake, Yellow=During Handover\n";
  }

  // Run simulation
  Simulator::Stop(simTime);
  Simulator::Run();

  // Final flow monitor check
  monitor->CheckForLostPackets();
  
  // Clean up
  Simulator::Destroy();

  // Clean up NetAnim
  if (anim)
  {
    delete anim;
  }

  // Close all files
  g_measCsv.close();
  g_enbRrcCsv.close();
  g_ueRrcCsv.close();
  g_mobilityTraceFile.close();
  g_throughputFile.close();
  g_handoverStatsFile.close();
  g_rsrpFile.close();
  g_baseStationFile.close();
  g_securityEventsFile.close();

  // Print final statistics
  PrintFinalStatistics();

  return 0;
}
