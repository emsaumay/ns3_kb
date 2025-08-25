#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/lte-module.h"
#include "ns3/point-to-point-helper.h"
#include <fstream>
#include <sstream>
#include "ns3/applications-module.h"

using namespace ns3;

static std::ofstream g_measCsv("meas_reports.csv");
static std::ofstream g_enbRrcCsv("enb_rrc_events.csv");
static std::ofstream g_ueRrcCsv("ue_rrc_events.csv");

static void
MeasReportSink(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti, LteRrcSap::MeasurementReport report)
{
  const auto& mr = report.measResults;   // shorthand

  // measId belongs to MeasResults (not MeasurementReport)
  uint8_t measId = mr.measId;

  // Serving cell quantized RSRP/RSRQ are on MeasResultPCell
  int srp = mr.measResultPCell.rsrpResult;   // 0..97 quantized per 36.331
  int srq = mr.measResultPCell.rsrqResult;   // 0..34  quantized per 36.331

  // Optional: detect if there are neighbour results present
  bool hasNeigh = mr.haveMeasResultNeighCells;

  // (tiny, robust label – we’re using A3 in the config)
  std::string event = hasNeigh ? "A3" : "NA";

  g_measCsv << Simulator::Now().GetSeconds() << ","
            << imsi << ","
            << cellId << ","
            << rnti << ","
            << unsigned(measId) << ","
            << event << ","
            << srp << ","
            << srq << std::endl;
}


static void EnbConnEstablished(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
  g_enbRrcCsv << "CONN_EST," << Simulator::Now().GetSeconds() << ","
              << imsi << "," << cellId << "," << rnti << std::endl;
}
static void EnbHoStart(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti, uint16_t targetCid)
{
  g_enbRrcCsv << "HO_START," << Simulator::Now().GetSeconds() << ","
              << imsi << "," << cellId << "," << rnti << ",to:" << targetCid << std::endl;
}
static void EnbHoEndOk(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
  g_enbRrcCsv << "HO_END_OK," << Simulator::Now().GetSeconds() << ","
              << imsi << "," << cellId << "," << rnti << std::endl;
}

static void UeConnEstablished(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
  g_ueRrcCsv << "UE_CONN_EST," << Simulator::Now().GetSeconds() << ","
             << imsi << "," << cellId << "," << rnti << std::endl;
}
static void UeHoStart(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti, uint16_t targetCid)
{
  g_ueRrcCsv << "UE_HO_START," << Simulator::Now().GetSeconds() << ","
             << imsi << "," << cellId << "," << rnti << ",to:" << targetCid << std::endl;
}
static void UeHoEndOk(std::string context, uint64_t imsi, uint16_t cellId, uint16_t rnti)
{
  g_ueRrcCsv << "UE_HO_END_OK," << Simulator::Now().GetSeconds() << ","
             << imsi << "," << cellId << "," << rnti << std::endl;
}

int main(int argc, char* argv[])
{
  Time simTime = Seconds(20.0);
  bool enableLogs = false;
  CommandLine cmd;
  cmd.AddValue("simTime", "Simulation time (s)", simTime);
  cmd.AddValue("enableLogs", "Turn on LTE logging", enableLogs);
  cmd.Parse(argc, argv);

  if (enableLogs)
  {
    LogComponentEnable("LteHelper", LOG_LEVEL_INFO);
  }

  // 0) EPC core and a remote host for traffic
  Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
  Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
  lteHelper->SetEpcHelper(epcHelper);

  Ptr<Node> pgw = epcHelper->GetPgwNode();
  NodeContainer remoteHostContainer;
  remoteHostContainer.Create(1);
  InternetStackHelper internet;
  internet.Install(remoteHostContainer);
  PointToPointHelper p2ph;
  p2ph.SetDeviceAttribute("DataRate", StringValue("10Gbps"));
  p2ph.SetChannelAttribute("Delay", StringValue("2ms"));
  NetDeviceContainer internetDevs = p2ph.Install(pgw, remoteHostContainer.Get(0));
  Ipv4AddressHelper ipv4h;
  ipv4h.SetBase("1.0.0.0", "255.0.0.0");
  Ipv4InterfaceContainer iic = ipv4h.Assign(internetDevs);
  Ipv4StaticRoutingHelper ipv4RoutingHelper;
  Ptr<Ipv4StaticRouting> remoteHostStaticRouting = ipv4RoutingHelper.GetStaticRouting(remoteHostContainer.Get(0)->GetObject<Ipv4>());
  remoteHostStaticRouting->AddNetworkRouteTo(Ipv4Address("7.0.0.0"), Ipv4Mask("255.0.0.0"), 1);

  // 1) Nodes: 3 eNBs (0=legit, 1=faulty legit, 2=fake/CSG) and 1 UE
  NodeContainer enbNodes;
  enbNodes.Create(3);
  NodeContainer ueNodes;
  ueNodes.Create(1);

  // 2) Positions (inline, 500 m apart)
  MobilityHelper mh;
  Ptr<ListPositionAllocator> pos = CreateObject<ListPositionAllocator>();
  pos->Add(Vector(0.0, 0.0, 0.0));     // eNB0 (legit)
  pos->Add(Vector(500.0, 0.0, 0.0));   // eNB1 (faulty)
  pos->Add(Vector(1000.0, 0.0, 0.0));  // eNB2 (fake)
  mh.SetPositionAllocator(pos);
  mh.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mh.Install(enbNodes);

  // UE moves from eNB0 towards eNB2 to trigger measurements
  MobilityHelper ueMob;
  ueMob.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
  ueMob.Install(ueNodes);
  ueNodes.Get(0)->GetObject<ConstantVelocityMobilityModel>()->SetPosition(Vector(-100.0, 0.0, 0.0));
  ueNodes.Get(0)->GetObject<ConstantVelocityMobilityModel>()->SetVelocity(Vector(30.0, 0.0, 0.0)); // 30 m/s

  // 3) Install LTE stacks
  NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice(enbNodes);
  NetDeviceContainer ueDevs  = lteHelper->InstallUeDevice(ueNodes);

  // 4) IP to UE via EPC
  internet.Install(ueNodes);
  Ipv4InterfaceContainer ueIpIfaces = epcHelper->AssignUeIpv4Address(NetDeviceContainer(ueDevs));
  for (uint32_t u = 0; u < ueNodes.GetN(); ++u)
  {
    Ptr<Ipv4StaticRouting> ueStaticRouting = ipv4RoutingHelper.GetStaticRouting(ueNodes.Get(u)->GetObject<Ipv4>());
    ueStaticRouting->SetDefaultRoute(epcHelper->GetUeDefaultGatewayAddress(), 1);
  }

  // 5) X2 neighbour links only among legit EPC cells (eNB0 <-> eNB1). The "fake" cell (eNB2) is not on X2.
  lteHelper->AddX2Interface(enbNodes.Get(0), enbNodes.Get(1));

  // 6) Handover algorithm: normal globally, then we distort the "faulty" node with Config::Set below
  lteHelper->SetHandoverAlgorithmType("ns3::A3RsrpHandoverAlgorithm");
  lteHelper->SetHandoverAlgorithmAttribute("Hysteresis", DoubleValue(3.0));     // dB
  lteHelper->SetHandoverAlgorithmAttribute("TimeToTrigger", TimeValue(MilliSeconds(160)));

  // 7) Attach UE initially to eNB0 (legitimate)
  lteHelper->Attach(ueDevs.Get(0), enbDevs.Get(0));

  // 8) Traffic (downlink UDP from remote host to UE)
  uint16_t dlPort = 1234;
  ApplicationContainer clientApps, serverApps;
  UdpServerHelper dlServer(dlPort);
  serverApps.Add(dlServer.Install(ueNodes.Get(0)));
  UdpClientHelper dlClient(ueIpIfaces.GetAddress(0), dlPort);
  dlClient.SetAttribute("Interval", TimeValue(MilliSeconds(20)));
  dlClient.SetAttribute("MaxPackets", UintegerValue(1000000));
  clientApps.Add(dlClient.Install(remoteHostContainer.Get(0)));
  serverApps.Start(Seconds(0.5));
  clientApps.Start(Seconds(0.6));

  // --------------------------
  // Fault injections / special configs
  // --------------------------

  // (A) Mark eNB2 as "fake"/rogue via Closed Subscriber Group (CSG) so the UE can see it but not attach.
  //     We enable CSG on eNB2 and leave UE with default CsgId=0, so access is denied (closed access).
  uint32_t enb2NodeId = enbNodes.Get(2)->GetId();
  Config::Set("/NodeList/" + std::to_string(enb2NodeId) + "/DeviceList/*/LteEnbNetDevice/CsgIndication", BooleanValue(true));
  Config::Set("/NodeList/" + std::to_string(enb2NodeId) + "/DeviceList/*/LteEnbNetDevice/CsgId", UintegerValue(777)); // arbitrary CSG

  // (B) Make eNB1 "faulty but legitimate":
  //     - Very low Tx power -> poor coverage / bad RSRP
  //     - Extreme handover parameters -> sluggish reporting/HO
  uint32_t enb1NodeId = enbNodes.Get(1)->GetId();
  Config::Set("/NodeList/" + std::to_string(enb1NodeId) + "/DeviceList/*/LteEnbPhy/TxPower", DoubleValue(10.0)); // dBm, unrealistically low
  Config::Set("/NodeList/" + std::to_string(enb1NodeId) + "/DeviceList/*/LteEnbRrc/HandoverAlgorithm/Hysteresis", DoubleValue(8.0));
  Config::Set("/NodeList/" + std::to_string(enb1NodeId) + "/DeviceList/*/LteEnbRrc/HandoverAlgorithm/TimeToTrigger", TimeValue(MilliSeconds(512)));

  // Optional: slightly higher Tx power on the legitimate eNB0 for clarity
  uint32_t enb0NodeId = enbNodes.Get(0)->GetId();
  Config::Set("/NodeList/" + std::to_string(enb0NodeId) + "/DeviceList/*/LteEnbPhy/TxPower", DoubleValue(43.0)); // ~20W

  // --------------------------
  // Tracing to CSV
  // --------------------------

  // Measurement reports at eNBs (this is the canonical trace to capture UE reports)
  // Signature: (uint64_t imsi, uint16_t cellId, uint16_t rnti, LteRrcSap::MeasurementReport msg)
  Config::Connect("/NodeList/*/DeviceList/*/LteEnbRrc/RecvMeasurementReport",
                  MakeCallback(&MeasReportSink));

  // eNB RRC events
  Config::Connect("/NodeList/*/DeviceList/*/LteEnbRrc/ConnectionEstablished",
                  MakeCallback(&EnbConnEstablished));
  Config::Connect("/NodeList/*/DeviceList/*/LteEnbRrc/HandoverStart",
                  MakeCallback(&EnbHoStart));
  Config::Connect("/NodeList/*/DeviceList/*/LteEnbRrc/HandoverEndOk",
                  MakeCallback(&EnbHoEndOk));

  // UE-side RRC events (handy for debugging state transitions)
  Config::Connect("/NodeList/*/DeviceList/*/LteUeRrc/ConnectionEstablished",
                  MakeCallback(&UeConnEstablished));
  Config::Connect("/NodeList/*/DeviceList/*/LteUeRrc/HandoverStart",
                  MakeCallback(&UeHoStart));
  Config::Connect("/NodeList/*/DeviceList/*/LteUeRrc/HandoverEndOk",
                  MakeCallback(&UeHoEndOk));

  // Header lines
  g_measCsv << "time,imsi,enbCellId,rnti,measId,event,servingRsrpQ,servingRsrqQ\n";
  g_enbRrcCsv << "event,time,imsi,cellId,rnti,info\n";
  g_ueRrcCsv  << "event,time,imsi,cellId,rnti,info\n";

  Simulator::Stop(simTime);
  Simulator::Run();
  Simulator::Destroy();

  g_measCsv.close();
  g_enbRrcCsv.close();
  g_ueRrcCsv.close();

  std::cout << "Wrote meas_reports.csv, enb_rrc_events.csv, ue_rrc_events.csv\n";
  return 0;
}
