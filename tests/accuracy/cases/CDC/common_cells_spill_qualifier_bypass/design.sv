// Copyright 2020 ETH Zurich and University of Bologna.
//
// Copyright and related rights are licensed under the Solderpad Hardware
// License, Version 0.51 (the "License"); you may not use this file except in
// compliance with the License. You may obtain a copy of the License at
// http://solderpad.org/licenses/SHL-0.51. Unless required by applicable law
// or agreed to in writing, software, hardware and materials distributed under
// this License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR THE
// specific language governing permissions and limitations under the License.
//
// Adapted from src/cc_isochronous_spill_register.sv @e73baaec2ca665cd80c3c384e9258e35242b829c.
// Reduced and modified for the SVerity golden set.

module top (
  input  logic       clk_a,
  input  logic       clk_b,
  input  logic       rst_a_ni,
  input  logic       rst_b_ni,
  input  logic       src_valid_a,
  input  logic       dst_ready_b,
  input  logic [7:0] src_data_a,
  output logic       dst_valid_b,
  output logic [7:0] dst_data_b
);
  logic [7:0] data_hold_a;
  logic token_a;
  logic token_s0_b;
  logic token_s1_b;
  logic token_seen_b;

  always_ff @(posedge clk_a or negedge rst_a_ni) begin
    if (!rst_a_ni) begin
      data_hold_a <= 8'h00;
      token_a <= 1'b0;
    end else if (src_valid_a) begin
      data_hold_a <= src_data_a;
      token_a <= ~token_a;
    end
  end

  always_ff @(posedge clk_b or negedge rst_b_ni) begin
    if (!rst_b_ni) begin
      token_s0_b <= 1'b0;
      token_s1_b <= 1'b0;
      token_seen_b <= 1'b0;
      dst_data_b <= 8'h00;
    end else begin
      token_s0_b <= token_a;
      token_s1_b <= token_s0_b;
      if (token_s1_b != token_seen_b) begin
        dst_data_b <= data_hold_a;
        token_seen_b <= token_s1_b;
      end
    end
  end

  assign dst_valid_b = src_valid_a;
endmodule
