// Adapted from lowRISC/common_cells src/cc_onehot.sv @e73baaec2ca665cd80c3c384e9258e35242b829c
// Copyright 2021 ETH Zurich.
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
  parameter int unsigned Width = 8
) (
  input  logic [Width-1:0] d_i,
  output logic             is_onehot_o
);
  logic [Width-1:0] lower_mask;

  assign lower_mask = d_i - {{Width-1{1'b0}}, 1'b1};
  // The reduction OR is intentional: at least one bit must be set before
  // the power-of-two test can classify the vector as one-hot.
  assign is_onehot_o = (|d_i) && ((d_i & lower_mask) == '0);
endmodule
