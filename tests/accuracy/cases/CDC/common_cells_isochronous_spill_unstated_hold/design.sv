// Adapted from lowRISC/common_cells src/cc_isochronous_spill_register.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
// Copyright 2020 ETH Zurich and University of Bologna.
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
  input logic src_clk_i, input logic dst_clk_i,
  input logic src_rst_ni, input logic dst_rst_ni,
  input logic src_valid_i, input logic [7:0] src_data_i,
  output logic dst_valid_o, output logic [7:0] dst_data_o
);
  logic [7:0] data_q;
  logic valid_meta_q, valid_sync_q;
  always_ff @(posedge src_clk_i or negedge src_rst_ni)
    if (!src_rst_ni) begin data_q <= 8'h00; valid_meta_q <= 1'b0; end
    else begin
      if (src_valid_i) data_q <= src_data_i;
      valid_meta_q <= src_valid_i;
    end
  always_ff @(posedge dst_clk_i or negedge dst_rst_ni)
    if (!dst_rst_ni) valid_sync_q <= 1'b0;
    else valid_sync_q <= valid_meta_q;
  assign dst_valid_o = valid_sync_q;
  assign dst_data_o = data_q;
endmodule
