// Copyright 2018 ETH Zurich and University of Bologna.
// Copyright and related rights are licensed under the Solderpad Hardware
// License, Version 0.51 (the "License"); you may not use this file except in
// compliance with the License. You may obtain a copy of the License at
// http://solderpad.org/licenses/SHL-0.51. Unless required by applicable law
// or agreed to in writing, software, hardware and materials distributed under
// this License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR THE
// specific language governing permissions and limitations under the License.
//
// Adapted from src/cc_edge_propagator_ack.sv @e73baaec2ca665cd80c3c384e9258e35242b829c.
// Reduced and modified for the SVerity golden set.

module top (
  input  logic clk_a,
  input  logic clk_b,
  input  logic rst_a_ni,
  input  logic rst_b_ni,
  input  logic edge_a,
  output logic req_toggle_a,
  output logic ack_toggle_b
);
  logic req_s0_b;
  logic req_s1_b;
  logic ack_s0_a;
  logic ack_s1_a;

  always_ff @(posedge clk_a or negedge rst_a_ni) begin
    if (!rst_a_ni) begin
      req_toggle_a <= 1'b0;
      ack_s0_a <= 1'b0;
      ack_s1_a <= 1'b0;
    end else begin
      ack_s0_a <= ack_toggle_b;
      ack_s1_a <= ack_s0_a;
      if (edge_a)
        req_toggle_a <= ~req_toggle_a;
    end
  end

  always_ff @(posedge clk_b or negedge rst_b_ni) begin
    if (!rst_b_ni) begin
      req_s0_b <= 1'b0;
      req_s1_b <= 1'b0;
      ack_toggle_b <= 1'b0;
    end else begin
      req_s0_b <= req_toggle_a;
      req_s1_b <= req_s0_b;
      if (req_s1_b != ack_toggle_b)
        ack_toggle_b <= req_s1_b;
    end
  end
endmodule
