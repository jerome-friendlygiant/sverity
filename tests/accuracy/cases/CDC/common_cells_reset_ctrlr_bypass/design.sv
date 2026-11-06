// Copyright 2021 ETH Zurich and University of Bologna.
//
// Copyright and related rights are licensed under the Solderpad Hardware
// License, Version 0.51 (the "License"); you may not use this file except in
// compliance with the License. You may obtain a copy of the License at
// http://solderpad.org/licenses/SHL-0.51. Unless required by applicable law
// or agreed to in writing, software, hardware and materials distributed under
// this License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR THE
// specific language governing permissions and limitations under the License.
//
// Adapted from src/cc_cdc_reset_ctrlr.sv @e73baaec2ca665cd80c3c384e9258e35242b829c.
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_a,
  input  logic clk_b,
  input  logic rst_b_ni,
  input  logic a_clear_i,
  input  logic b_ack_i,
  output logic b_clear_o,
  output logic b_isolate_o
);
  logic clear_s0_b;
  logic clear_s1_b;

  always_ff @(posedge clk_b or negedge rst_b_ni) begin
    if (!rst_b_ni) begin
      clear_s0_b <= 1'b0;
      clear_s1_b <= 1'b0;
      b_isolate_o <= 1'b0;
    end else begin
      clear_s0_b <= a_clear_i;
      clear_s1_b <= clear_s0_b;
      if (b_ack_i)
        b_isolate_o <= clear_s1_b;
    end
  end

  assign b_clear_o = a_clear_i;
endmodule
