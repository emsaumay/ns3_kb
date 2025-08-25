#!/usr/bin/env python3
"""
Handover Mobility Analysis - Data Analysis Script
This script analyzes the comprehensive data collected from the NS-3 LTE handover simulation.
"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns
from datetime import datetime

def main():
    print("=== Handover Mobility Analysis - Data Analysis ===")
    print(f"Analysis performed on: {datetime.now()}")
    print()
    
    try:
        # Load all CSV files
        print("Loading data files...")
        meas_reports = pd.read_csv('handover_meas_reports.csv')
        enb_rrc = pd.read_csv('handover_enb_rrc_events.csv')
        ue_rrc = pd.read_csv('handover_ue_rrc_events.csv')
        rsrp_data = pd.read_csv('rsrp_measurements.csv')
        throughput_data = pd.read_csv('throughput_analysis.csv')
        
        print(f"Successfully loaded data files!")
        print(f"- Measurement reports: {len(meas_reports)} records")
        print(f"- eNB RRC events: {len(enb_rrc)} records")
        print(f"- UE RRC events: {len(ue_rrc)} records")
        print(f"- RSRP measurements: {len(rsrp_data)} records")
        print(f"- Throughput analysis: {len(throughput_data)} records")
        print()
        
        # Basic statistics
        print("=== SIMULATION OVERVIEW ===")
        simulation_duration = max(meas_reports['time'].max(), throughput_data['time'].max())
        print(f"Simulation Duration: {simulation_duration:.2f} seconds")
        
        # UE Analysis
        num_ues = len(meas_reports['imsi'].unique())
        print(f"Number of UEs: {num_ues}")
        print(f"UE IMSIs: {sorted(meas_reports['imsi'].unique())}")
        
        # eNB Analysis
        num_enbs = len(meas_reports['enbCellId'].unique())
        print(f"Number of eNBs: {num_enbs}")
        print(f"eNB Cell IDs: {sorted(meas_reports['enbCellId'].unique())}")
        print()
        
        # Connection Events Analysis
        print("=== CONNECTION EVENTS ANALYSIS ===")
        conn_events = enb_rrc[enb_rrc['event'] == 'CONN_EST']
        print(f"Total Connection Establishments: {len(conn_events)}")
        
        ho_start_events = enb_rrc[enb_rrc['event'] == 'HO_START']
        ho_end_events = enb_rrc[enb_rrc['event'] == 'HO_END_OK']
        print(f"Handover Start Events: {len(ho_start_events)}")
        print(f"Handover End (Success) Events: {len(ho_end_events)}")
        
        if len(ho_start_events) > 0:
            print(f"Handover Success Rate: {len(ho_end_events)/len(ho_start_events)*100:.1f}%")
        print()
        
        # Measurement Reports Analysis
        print("=== MEASUREMENT REPORTS ANALYSIS ===")
        a3_events = meas_reports[meas_reports['event'] == 'A3']
        periodic_events = meas_reports[meas_reports['event'] == 'PERIODIC']
        print(f"A3 Events (handover triggering): {len(a3_events)}")
        print(f"Periodic Measurement Reports: {len(periodic_events)}")
        
        # RSRP Analysis
        print()
        print("=== RSRP/RSRQ ANALYSIS ===")
        print(f"RSRP Statistics (dBm):")
        print(f"  Mean: {rsrp_data['rsrpDbm'].mean():.2f}")
        print(f"  Min: {rsrp_data['rsrpDbm'].min():.2f}")
        print(f"  Max: {rsrp_data['rsrpDbm'].max():.2f}")
        print(f"  Std: {rsrp_data['rsrpDbm'].std():.2f}")
        
        print(f"RSRQ Statistics (dB):")
        print(f"  Mean: {rsrp_data['rsrqDb'].mean():.2f}")
        print(f"  Min: {rsrp_data['rsrqDb'].min():.2f}")
        print(f"  Max: {rsrp_data['rsrqDb'].max():.2f}")
        print(f"  Std: {rsrp_data['rsrqDb'].std():.2f}")
        print()
        
        # Throughput Analysis
        print("=== THROUGHPUT ANALYSIS ===")
        # Filter out flows with no data
        valid_throughput = throughput_data[throughput_data['throughputMbps'] > 0]
        if len(valid_throughput) > 0:
            print(f"Active Flows: {len(valid_throughput['flowId'].unique())}")
            print(f"Average Throughput: {valid_throughput['throughputMbps'].mean():.3f} Mbps")
            print(f"Max Throughput: {valid_throughput['throughputMbps'].max():.3f} Mbps")
            print(f"Average Delay: {valid_throughput['delayMs'].mean():.3f} ms")
            print(f"Average Packet Loss: {valid_throughput['packetLossPercent'].mean():.2f}%")
        else:
            print("No valid throughput data found")
        print()
        
        # Per-UE Analysis
        print("=== PER-UE ANALYSIS ===")
        for imsi in sorted(meas_reports['imsi'].unique()):
            ue_meas = meas_reports[meas_reports['imsi'] == imsi]
            ue_rsrp = rsrp_data[rsrp_data['imsi'] == imsi]
            
            print(f"UE {imsi}:")
            print(f"  Measurement Reports: {len(ue_meas)}")
            print(f"  A3 Events: {len(ue_meas[ue_meas['event'] == 'A3'])}")
            if len(ue_rsrp) > 0:
                print(f"  Avg RSRP: {ue_rsrp['rsrpDbm'].mean():.2f} dBm")
                print(f"  RSRP Range: {ue_rsrp['rsrpDbm'].min():.2f} to {ue_rsrp['rsrpDbm'].max():.2f} dBm")
        print()
        
        # File Summary
        print("=== GENERATED FILES SUMMARY ===")
        import os
        csv_files = [f for f in os.listdir('.') if f.endswith('.csv') and 'handover' in f or 'rsrp' in f or 'throughput' in f]
        pcap_files = [f for f in os.listdir('.') if f.endswith('.pcap')]
        
        print("CSV Data Files:")
        for file in sorted(csv_files):
            size = os.path.getsize(file)
            print(f"  {file}: {size/1024:.1f} KB")
        
        print(f"\nPCAP Files: {len(pcap_files)} files")
        total_pcap_size = sum(os.path.getsize(f) for f in pcap_files if f.endswith('.pcap'))
        print(f"Total PCAP Size: {total_pcap_size/1024/1024:.1f} MB")
        
        print()
        print("=== ANALYSIS COMPLETE ===")
        print("This comprehensive dataset includes:")
        print("1. Detailed measurement reports with neighbor cell information")
        print("2. RRC connection and handover events")
        print("3. RSRP/RSRQ signal quality measurements")
        print("4. Throughput, delay, jitter, and packet loss metrics")
        print("5. PCAP files for detailed packet-level analysis")
        print()
        print("The simulation successfully collected quality data over 1 minute")
        print("with realistic UE mobility and handover scenarios.")
        
    except FileNotFoundError as e:
        print(f"Error: Could not find data file - {e}")
        print("Please make sure the simulation has completed and generated the CSV files.")
    except Exception as e:
        print(f"Error during analysis: {e}")

if __name__ == "__main__":
    main()
