// Adapted from lowRISC/common_cells src/cc_shift_register.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
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

module top #(
  parameter int unsigned Depth = 3,
  parameter int unsigned DataWidth = 8
) (
  input  logic                 clk_i,
  input  logic                 rst_ni,
  input  logic                 clr_i,
  input  logic [DataWidth-1:0] d_i,
  output logic [DataWidth-1:0] d_o
);
  logic [DataWidth-1:0] stages [Depth];

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      for (int i = 0; i < Depth; i++) stages[i] <= '0;
    end else if (clr_i) begin
      for (int i = 0; i < Depth; i++) stages[i] <= '0;
    end else begin
      stages[0] <= d_i;
      for (int i = 1; i < Depth; i++) stages[i] <= stages[i-1];
    end
  end

  assign d_o = stages[Depth-1];
endmodule
