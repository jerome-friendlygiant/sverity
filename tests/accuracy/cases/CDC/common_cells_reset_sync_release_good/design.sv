// Adapted from lowRISC/common_cells src/cc_rstgen.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
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
  input logic clk_i, input logic async_rst_ni, input logic test_mode_i,
  output logic rst_no, output logic init_no
);
  logic [1:0] release_q;
  always_ff @(posedge clk_i or negedge async_rst_ni) begin
    if (!async_rst_ni) release_q <= 2'b00;
    else release_q <= {release_q[0], 1'b1};
  end
  assign rst_no = ~(release_q[1] | test_mode_i);
  assign init_no = release_q[1];
endmodule
