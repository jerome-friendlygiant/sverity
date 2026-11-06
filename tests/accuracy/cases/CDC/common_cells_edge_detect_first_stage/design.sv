// Copyright 2018 ETH Zurich and University of Bologna.
// Copyright and related rights are licensed under the Solderpad Hardware
// License, Version 0.51 (the "License"); you may not use this file except in
// compliance with the License. You may obtain a copy of the License at
// http://solderpad.org/licenses/SHL-0.51. Unless required by applicable law
// or agreed to in writing, software, hardware and materials distributed under
// this License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR THE
// specific language governing permissions and limitations under the License.
//
// Adapted from src/cc_edge_detect.sv @e73baaec2ca665cd80c3c384e9258e35242b829c.
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_a,
  input  logic clk_b,
  input  logic rst_b_ni,
  input  logic d_a,
  output logic rising_b
);
  logic stage0_b;
  logic stage1_b;
  logic prior_b;

  always_ff @(posedge clk_b or negedge rst_b_ni) begin
    if (!rst_b_ni) begin
      stage0_b <= 1'b0;
      stage1_b <= 1'b0;
      prior_b <= 1'b0;
    end else begin
      stage0_b <= d_a;
      stage1_b <= stage0_b;
      prior_b <= stage1_b;
    end
  end

  assign rising_b = stage0_b & ~prior_b;
endmodule
