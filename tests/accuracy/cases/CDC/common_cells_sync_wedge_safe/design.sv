// Adapted from lowRISC/common_cells src/cc_sync_wedge.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
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
  input  logic clk_i,
  input  logic rst_ni,
  input  logic async_level_i,
  output logic level_o,
  output logic rising_o,
  output logic falling_o
);
  logic sync_first_q, sync_level_q, sync_level_prev_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      sync_first_q     <= 1'b0;
      sync_level_q     <= 1'b0;
      sync_level_prev_q <= 1'b0;
    end else begin
      sync_first_q      <= async_level_i;
      sync_level_q      <= sync_first_q;
      sync_level_prev_q <= sync_level_q;
    end
  end

  assign level_o  = sync_level_q;
  assign rising_o = sync_level_q & ~sync_level_prev_q;
  assign falling_o = ~sync_level_q & sync_level_prev_q;
endmodule
