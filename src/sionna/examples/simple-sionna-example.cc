#include "ns3/config-store-module.h"
#include "ns3/core-module.h"
#include "ns3/lte-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/sionna-helper.h"
#include "ns3/sionna-lte-pathloss-model.h"
#include "ns3/sionna-helper.h"


using namespace ns3;

NS_LOG_COMPONENT_DEFINE("LteRandomMobilityAndTrafficWithSionna");


static void
SendLocUpdateToSionna(Ptr<Node> node)
{
    // node->GetId() 可能是 eNB=0, UE=1,2,3...
    // 这里随意给 "objxxx" 当做 Sionna 端的对象名
    std::string obj_id = "obj" + std::to_string(node->GetId());

    // 获取位置、速度
    Ptr<MobilityModel> mob = node->GetObject<MobilityModel>();
    Vector pos = mob->GetPosition();
    Vector vel = mob->GetVelocity();

    // 调用 sionna-connection-handler.cc 里的 updateLocationInSionna(...)
    // 这会发出 "LOC_UPDATE:objX, x, y, z, angle" 消息
    updateLocationInSionna(obj_id, pos, vel);

    NS_LOG_UNCOND("Sent LOC_UPDATE for " << obj_id
                 << " at (" << pos.x << "," << pos.y << "," << pos.z << ")");
}

int main(int argc, char* argv[])
{
    double txPower = 0;  // dBm // 发射功率 // 1 mW
    uint16_t earfcn = 100;  // EARFCN (2100 MHz) // 对应 LTE 频率 2.1GHz 左右。

    bool sionna = true; // Use Sionna // 是否启用 Sionna 射线追踪。
    std::string server_ip = "127.0.0.1"; //localhost // Sionna 服务器 (Python) 在本机
    bool local_machine = true; // 表示 ns-3 与 Python 在同一机器上
    bool verb = false; // 是否开启详细日志

    CommandLine cmd(__FILE__);
    cmd.AddValue("txPower", "TX power in dBm", txPower);
    cmd.AddValue("earfcn", "EARFCN (default 100 - 2.1GHz)", earfcn);
    // Command Line Parameters for Sionna
    cmd.AddValue ("sionna", "Use and enable Sionna as channel", sionna);
    cmd.AddValue ("sionna-server-ip", "Sionna server IP Address", server_ip);
    cmd.AddValue ("sionna-local-machine", "Set to True if Sionna is running locally", local_machine);
    cmd.AddValue ("sionna-verbose", "Enable verbose logging to compare values from Sionna and ns-3 channel model", verb);
    cmd.Parse(argc, argv);

    // Create SionnaHelper
    SionnaHelper& sionnaHelper = SionnaHelper::GetInstance();

    if (sionna)
    {
        sionnaHelper.SetSionna(sionna); // Enable Sionna
        sionnaHelper.SetServerIp(server_ip); // Set server IP (in this case, localhost)
        sionnaHelper.SetLocalMachine(local_machine); // Set True if Sionna is running locally, as in this example
        sionnaHelper.SetVerbose(verb); // Enable verbose logging
    }

    Ptr<LteHelper> lteHelper = CreateObject<LteHelper>();
    lteHelper->SetSpectrumChannelType("ns3::SingleModelSpectrumChannel");
    lteHelper->SetSpectrumChannelAttribute("SpectrumPropagationLossModel",
    StringValue("ns3::SionnaLtePathlossModel"));


    Ptr<PointToPointEpcHelper> epcHelper = CreateObject<PointToPointEpcHelper>();
    lteHelper->SetEpcHelper(epcHelper);
    lteHelper->SetEnbDeviceAttribute("DlEarfcn", UintegerValue(earfcn));
    lteHelper->SetEnbDeviceAttribute("UlEarfcn", UintegerValue(earfcn + 18000)); // DlEarfcn / UlEarfcn 设置上下行频点 (100, 18100...) 大约在 2.1GHz 频段。

    NodeContainer enbNodes;
    NodeContainer ueNodes;
    enbNodes.Create(1);
    ueNodes.Create(3); // 创建一个 eNB 节点，3 个 UE 节点

    // Introduce mobility for eNB (static) and UEs (random walk, for example)
    MobilityHelper mobilityEnb;
    mobilityEnb.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobilityEnb.Install(enbNodes);


    enbNodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(0.0, 0.0, 1.5)); // 给 eNB 用 ConstantPositionMobilityModel，并把它固定在 (x=0, y=0, z=1.5)。

    // 同理，3 个 UE 也用 ConstantPosition；分别固定在 (200, 0, 1.5), (200, 10, 1.5), (200, -10, 1.5)。
    // 这样就保证 eNB & UE 之间可以穿过一些建筑（在 scene.xml 里），从而得到有衰减的射线结果。
    MobilityHelper mobilityUe;
    mobilityUe.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobilityUe.Install(ueNodes);


    ueNodes.Get(0)->GetObject<MobilityModel>()->SetPosition(Vector(200.0, 0.0, 1.5));
    ueNodes.Get(1)->GetObject<MobilityModel>()->SetPosition(Vector(200.0, 10.0, 1.5));
    ueNodes.Get(2)->GetObject<MobilityModel>()->SetPosition(Vector(200.0, -10.0, 1.5));



    // Install Internet stack on UEs
    InternetStackHelper internet;
    internet.Install(ueNodes); // 在 UE 节点上装 TCP/IP 协议栈（UDP, TCP, IP 等）

    // Create and install LTE devices
    NetDeviceContainer enbDevs = lteHelper->InstallEnbDevice(enbNodes);
    NetDeviceContainer ueDevs = lteHelper->InstallUeDevice(ueNodes);
    // 调用 lteHelper->InstallEnbDevice() & InstallUeDevice() 分别给 eNB 和 UE 节点装 LTE NetDevice
    // (相当于 4G Modem / eNB MAC+PHY)。这一步会自动设置一些默认信道、PHY、MAC等配置。

    // Set TX power for eNB and UEs
    enbDevs.Get(0)->GetObject<LteEnbNetDevice>()->GetPhy()->SetTxPower(txPower);
    for (uint32_t i = 0; i < ueDevs.GetN(); ++i) {
        ueDevs.Get(i)->GetObject<LteUeNetDevice>()->GetPhy()->SetTxPower(txPower);
    }

    // Attach UEs to eNB
    // 把每个 UE 都连到第 0 号 eNB（你只有 1 个 eNB）。这样 UE 就能进行无线通信
    for (uint32_t i = 0; i < ueNodes.GetN(); i++) {
        lteHelper->Attach(ueDevs.Get(i), enbDevs.Get(0));
    }

    // Generate UDP traffic
    uint16_t port = 9;
    for (uint32_t i = 0; i < ueNodes.GetN(); ++i) { // 对每一个 i，我们将把它当成UDP 服务器来部署一个 UdpEchoServer。
        UdpEchoServerHelper echoServer(port);
        ApplicationContainer serverApps = echoServer.Install(ueNodes.Get(i)); // 在第 i 号 UE 上安装这个 EchoServer 应用。
        serverApps.Start(Seconds(1.0));
        serverApps.Stop(Seconds(20.0)); // 这个服务器应用从1秒开始启用，一直运行到20秒才停止

        for (uint32_t j = 0; j < ueNodes.GetN(); ++j) {
            if (i != j) { // j != i，就代表“这个 j 节点不是服务器节点 i 本身”，那就给 j 节点装一个客户端（Client），让它向 i 节点发送数据
                // UE 发往 “7.0.0.x” 会自动通过 EPC 解析到对应的 UE IP（PointToPointEpcHelper 做了路由/NAT），
                // 最后转发到安装了 UdpEchoServer 的节点 i
                UdpEchoClientHelper echoClient(Ipv4Address("7.0.0.1"), port);
                echoClient.SetAttribute("MaxPackets", UintegerValue(10)); // 客户端只发 10 个包，然后结束发送
                // 发送间隔
                echoClient.SetAttribute("Interval", TimeValue(Seconds(CreateObject<UniformRandomVariable>()->GetValue(1.0, 3.0))));
                // UDP 包是 1024字节
                echoClient.SetAttribute("PacketSize", UintegerValue(1024));

                ApplicationContainer clientApps = echoClient.Install(ueNodes.Get(j)); // 把这个 Client 应用安装到 “UE j” 节点上
                clientApps.Start(Seconds(2.0 + j));
                clientApps.Stop(Seconds(20.0));
            }
        }
    }

    // ======== 在启动仿真前，把 eNB 和 UE 坐标更新发送到 Sionna ========
    // 这样 Python 端会收到 LOC_UPDATE: 并 respond with LOC_CONFIRM:...
    NS_LOG_UNCOND("=== Sending LOC_UPDATE for eNB and UEs ===");
//     eNB
//    SendLocUpdateToSionna(enbNodes.Get(0));
//    // UEs
//    for (uint32_t i=0; i<ueNodes.GetN(); ++i) {
//        SendLocUpdateToSionna(ueNodes.Get(i));
//    }

    // 1) 停止仿真在 2s
    // 仿真时长 2 秒，然后收集 LTE PHY、MAC、RLC 等层面的一些默认追踪数据。
    Simulator::Stop(Seconds(10.0));
    lteHelper->EnablePhyTraces();
    lteHelper->EnableMacTraces();
    lteHelper->EnableRlcTraces();


    // 2) 运行仿真
    // Simulator::Run() 会一直执行事件调度，直到到达 2.0 秒。
    Simulator::Run();

    // =============================
    // 在 Destroy 前做:
    //    - 获取节点位置
    //    - 调用 Sionna 请求 path loss
    // =============================
    NS_LOG_UNCOND("=== Simulation ended at t=2s ===");

    Ptr<MobilityModel> enbMob = enbNodes.Get(0)->GetObject<MobilityModel>();
    Vector enbPos = enbMob->GetPosition();

    // 这段是在IP/应用层让 UE 间互发回显流量
    // 但底层物理发送实际是 UE → eNB → EPC → eNB → UE
    for (uint32_t i = 0; i < ueNodes.GetN(); ++i)
    {
        Ptr<MobilityModel> ueMob = ueNodes.Get(i)->GetObject<MobilityModel>();
        Vector uePos = ueMob->GetPosition();

        // (A) PathLoss 发射机 (Tx) 到接收机 (Rx) 之间的信号整体衰减（以 dB 为单位）
        double pathLossDb = getPathGainFromSionna(enbPos, uePos);

        // (B) Delay 发送端到接收端的“最短物理传播时延
        double delay_s = getPropagationDelayFromSionna(enbPos, uePos);

        // (C) LOS or not
        // 检查直视视距
        std::string losInfo = getLOSStatusFromSionna(enbPos, uePos);

        std::cout << "UE " << i << ": "
                  << "PathLoss=" << pathLossDb << " dB, "
                  << "Delay=" << (delay_s*1e6) << " us, "  // convert to microseconds
                  << "LOS= " << losInfo << std::endl;
    }

    // 4) 再 Destroy
    Simulator::Destroy();
    // 3) 关闭 Sionna 端
    sionnaHelper.ShutdownSionna();

    return 0;
}
