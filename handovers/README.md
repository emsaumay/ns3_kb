# LTE Handover and Rogue Base Station Analysis Suite

This repository contains three progressive NS-3 simulation scripts for analyzing LTE handover behavior and security vulnerabilities in cellular networks. Each script builds upon the previous one, adding increasing levels of complexity and analytical capabilities.

## Overview

### Files Structure
1. **`rogue-enb.cc`** - Basic rogue base station detection scenario
2. **`handover-mobility-analysis.cc`** - Enhanced handover analysis with mobility
3. **`comprehensive-handover-analysis.cc`** - Complete security analysis with visualization

## Script Descriptions

### 1. rogue-enb.cc - Foundation Script

**Purpose**: Demonstrates basic rogue base station behavior and detection mechanisms in LTE networks.

**Key Features**:
- 3 Base Stations: 1 legitimate, 1 faulty legitimate, 1 fake/rogue
- Single UE with linear mobility pattern
- Basic measurement report collection
- CSG (Closed Subscriber Group) based rogue BS simulation
- Simple fault injection mechanisms

**Technical Details**:
- **Simulation Time**: 20 seconds
- **Topology**: Linear arrangement (500m spacing)
- **UE Speed**: 30 m/s constant velocity
- **Handover Algorithm**: A3 RSRP with basic parameters
- **Security Model**: CSG-based access control for rogue BS

**Generated Files**:
- `meas_reports.csv` - Basic measurement reports
- `enb_rrc_events.csv` - eNB RRC state transitions
- `ue_rrc_events.csv` - UE RRC state transitions

### 2. handover-mobility-analysis.cc - Enhanced Analysis

**Purpose**: Extends the basic scenario with comprehensive handover analysis and realistic mobility patterns.

**Key Features**:
- 5 Base Stations for increased handover opportunities
- 4 UEs with diverse mobility patterns
- Enhanced measurement reporting with RSRP/RSRQ conversion
- Throughput and QoS monitoring
- Flow-based traffic analysis
- PCAP packet capture support

**Technical Improvements**:
- **Simulation Time**: 60 seconds
- **Topology**: Extended linear with 300m spacing
- **UE Patterns**: Mixed linear and random mobility
- **Traffic**: UDP/TCP mixed applications
- **Monitoring**: Real-time throughput analysis
- **Data Collection**: 7 comprehensive CSV files

**Generated Files**:
- `handover_meas_reports.csv` - Enhanced measurement data
- `handover_enb_rrc_events.csv` - eNB events with timing
- `handover_ue_rrc_events.csv` - UE events with timing
- `ue_mobility_trace.csv` - Position and velocity tracking
- `throughput_analysis.csv` - QoS metrics
- `handover_statistics.csv` - Handover timing analysis
- `rsrp_measurements.csv` - Detailed signal quality data

### 3. comprehensive-handover-analysis.cc - Complete Security Suite

**Purpose**: Provides comprehensive security analysis with base station classification and visual simulation.

**Key Features**:
- **Base Station Classification**: Legitimate, Faulty, Fake/Rogue
- **NetAnim Integration**: Visual simulation with real-time animation
- **Security Event Logging**: Comprehensive security incident tracking
- **Advanced Mobility**: Strategic patterns to interact with all BS types
- **Enhanced Analytics**: Security-focused data collection

**Advanced Capabilities**:
- **Simulation Time**: 120 seconds (extended for thorough analysis)
- **BS Types**: Configurable numbers of each type
- **UE Behavior**: Intelligent mobility to test all scenarios
- **Visualization**: Real-time NetAnim animation
- **Security Focus**: Detailed security event classification

**Generated Files**:
- `comprehensive_meas_reports.csv` - BS-classified measurement data
- `comprehensive_enb_rrc_events.csv` - Enhanced eNB events
- `comprehensive_ue_rrc_events.csv` - Enhanced UE events
- `comprehensive_ue_mobility_trace.csv` - Extended mobility data
- `comprehensive_throughput_analysis.csv` - QoS with security context
- `comprehensive_handover_statistics.csv` - Security-aware handover stats
- `comprehensive_rsrp_measurements.csv` - Signal quality with BS classification
- `comprehensive_base_station_info.csv` - BS configuration and classification
- `comprehensive_security_events.csv` - Security incident log
- `comprehensive-handover-analysis.xml` - NetAnim visualization file

## Building and Running

### Prerequisites
- NS-3.45 or later
- C++17 compatible compiler
- NetAnim (for visualization of comprehensive analysis)

### Building the Scripts

```bash
# Navigate to NS-3 directory
cd /path/to/ns-3.45

# Build individual scripts
./ns3 build

# Or build specific targets
./ns3 configure --enable-examples
./ns3 build
```

### Running the Simulations

#### 1. Basic Rogue eNB Analysis
```bash
# Basic run
./ns3 run scratch/rogue-enb

# With logging enabled
./ns3 run "scratch/rogue-enb --enableLogs=1"

# Custom simulation time
./ns3 run "scratch/rogue-enb --simTime=30"
```

#### 2. Handover Mobility Analysis
```bash
# Default configuration
./ns3 run scratch/handover-mobility-analysis

# Extended simulation with more UEs
./ns3 run "scratch/handover-mobility-analysis --numUes=6 --numEnbs=7 --simTime=90"

# Enable PCAP tracing
./ns3 run "scratch/handover-mobility-analysis --enablePcap=1"

# Adjust UE speed
./ns3 run "scratch/handover-mobility-analysis --ueSpeed=25"
```

#### 3. Comprehensive Security Analysis
```bash
# Default comprehensive analysis
./ns3 run scratch/comprehensive-handover-analysis

# Custom base station configuration
./ns3 run "scratch/comprehensive-handover-analysis --numLegitEnbs=3 --numFaultyEnbs=2 --numFakeEnbs=2"

# Extended simulation for thorough analysis
./ns3 run "scratch/comprehensive-handover-analysis --simTime=180 --numUes=5"

# Disable NetAnim for faster execution
./ns3 run "scratch/comprehensive-handover-analysis --enableNetAnim=0"

# Enable PCAP for detailed packet analysis
./ns3 run "scratch/comprehensive-handover-analysis --enablePcap=1"
```

## Scenario Analysis

### Security Scenarios Tested

1. **Legitimate Handover**: Normal operation between legitimate base stations
2. **Faulty BS Interaction**: Handovers involving degraded legitimate base stations
3. **Rogue BS Detection**: UE interaction with fake/malicious base stations
4. **Mixed Environment**: Realistic scenarios with all three BS types

### Key Metrics Collected

- **Signal Quality**: RSRP, RSRQ measurements with dBm/dB conversion
- **Handover Performance**: Success rates, timing, failure analysis
- **Security Events**: Unauthorized access attempts, suspicious behavior
- **QoS Impact**: Throughput degradation due to security threats
- **Mobility Patterns**: UE movement correlation with handover behavior

## Visualization

### NetAnim Usage (Comprehensive Analysis)
```bash
# After running comprehensive analysis
netanim comprehensive-handover-analysis.xml
```

**NetAnim Features**:
- Real-time UE movement visualization
- Base station classification color coding
- Handover event animation
- Signal strength indicators

## Data Analysis

### Python Analysis Scripts
The repository includes analysis scripts for processing the generated CSV files:

```bash
# Basic analysis
python3 simple_analysis.py

# Comprehensive analysis with plots
python3 analyze_comprehensive_results.py

# Compare results across simulations
python3 analyze_results.py
```


## Configuration Options

### Command Line Parameters

| Parameter | Script | Description | Default |
|-----------|---------|-------------|---------|
| `simTime` | All | Simulation duration (seconds) | 20/60/120 |
| `numUes` | Enhanced/Comprehensive | Number of user devices | 4/3 |
| `numEnbs` | Enhanced | Total base stations | 5 |
| `numLegitEnbs` | Comprehensive | Legitimate base stations | 2 |
| `numFaultyEnbs` | Comprehensive | Faulty legitimate BSs | 1 |
| `numFakeEnbs` | Comprehensive | Rogue/fake base stations | 1 |
| `ueSpeed` | Enhanced/Comprehensive | UE velocity (m/s) | 15 |
| `enableLogs` | All | Enable NS-3 logging | false |
| `enablePcap` | Enhanced/Comprehensive | Enable packet capture | false |
| `enableNetAnim` | Comprehensive | Enable visualization | true |

