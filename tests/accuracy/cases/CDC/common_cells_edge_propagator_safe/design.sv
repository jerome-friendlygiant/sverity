// Adapted from lowRISC/common_cells src/cc_edge_propagator.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
// Copyright 2018 ETH Zurich and University of Bologna.
// Copyright and related rights are licensed under the Solderpad Hardware
// License, Version 0.51 (the "License"); you may not use this file except in
// compliance with the License. You may obtain a copy of the License at
// http://solderpad.org/licenses/SHL-0.51. Unless required by applicable law
// or agreed to in writing, software, hardware and materials distributed under
// this License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.
// Reduced and modified for the SVerity golden set.

module top (
  input logic clk_tx_i, input logic rst_tx_ni, input logic edge_i,
  input logic clk_rx_i, input logic rst_rx_ni, output logic edge_o
);
  logic tx_toggle_q, rx_meta_q, rx_toggle_q, rx_seen_q, rx_prev_q;
  logic ack_meta_q, ack_sync_q;
  logic tx_busy;

  always_ff @(posedge clk_tx_i or negedge rst_tx_ni) begin
    if (!rst_tx_ni) begin
      tx_toggle_q <= 1'b0;
      ack_meta_q <= 1'b0;
      ack_sync_q <= 1'b0;
    end else begin
      ack_meta_q <= rx_seen_q;
      ack_sync_q <= ack_meta_q;
      if (edge_i && !tx_busy) tx_toggle_q <= ~tx_toggle_q;
    end
  end
  assign tx_busy = tx_toggle_q ^ ack_sync_q;

  always_ff @(posedge clk_rx_i or negedge rst_rx_ni) begin
    if (!rst_rx_ni) begin
      rx_meta_q <= 1'b0;
      rx_toggle_q <= 1'b0;
      rx_seen_q <= 1'b0;
      rx_prev_q <= 1'b0;
    end else begin
      rx_meta_q <= tx_toggle_q;
      rx_toggle_q <= rx_meta_q;
      rx_prev_q <= rx_toggle_q;
      if (rx_toggle_q != rx_seen_q) rx_seen_q <= rx_toggle_q;
    end
  end
  assign edge_o = rx_toggle_q ^ rx_prev_q;
endmodule
