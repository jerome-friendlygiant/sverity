// Adapted from lowRISC/common_cells src/cc_clk_mux_glitch_free.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
// Copyright (C) 2013-2022 ETH Zurich, University of Bologna
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
  input logic clk_a_i, input logic clk_b_i, input logic rst_ni,
  input logic select_b_i, output logic clk_o
);
  logic sel_a_meta_q, sel_a_q, sel_b_meta_q, sel_b_q;
  logic en_a_q, en_b_q, en_b_a_meta_q, en_b_a_q, en_a_b_meta_q, en_a_b_q;

  always_ff @(posedge clk_a_i or negedge rst_ni)
    if (!rst_ni) begin sel_a_meta_q <= 1'b0; sel_a_q <= 1'b0; end
    else begin sel_a_meta_q <= select_b_i; sel_a_q <= sel_a_meta_q; end
  always_ff @(posedge clk_b_i or negedge rst_ni)
    if (!rst_ni) begin sel_b_meta_q <= 1'b0; sel_b_q <= 1'b0; end
    else begin sel_b_meta_q <= select_b_i; sel_b_q <= sel_b_meta_q; end
  always_ff @(posedge clk_a_i or negedge rst_ni)
    if (!rst_ni) begin en_b_a_meta_q <= 1'b0; en_b_a_q <= 1'b0; end
    else begin en_b_a_meta_q <= en_b_q; en_b_a_q <= en_b_a_meta_q; end
  always_ff @(posedge clk_b_i or negedge rst_ni)
    if (!rst_ni) begin en_a_b_meta_q <= 1'b0; en_a_b_q <= 1'b0; end
    else begin en_a_b_meta_q <= en_a_q; en_a_b_q <= en_a_b_meta_q; end

  always_ff @(negedge clk_a_i or negedge rst_ni)
    if (!rst_ni) en_a_q <= 1'b0;
    else en_a_q <= ~sel_a_q & ~en_b_a_q;
  always_ff @(negedge clk_b_i or negedge rst_ni)
    if (!rst_ni) en_b_q <= 1'b0;
    else en_b_q <= sel_b_q & ~en_a_b_q;

  assign clk_o = (clk_a_i & en_a_q) | (clk_b_i & en_b_q);
endmodule
